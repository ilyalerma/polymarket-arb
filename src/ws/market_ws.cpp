#include "pm/ws/market_ws.hpp"

#include "pm/fee_model.hpp"
#include "pm/trade_fire.hpp"
#include "pm/ws/simdjson_book_parser.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include <simdjson.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace pm {

MarketWs::MarketWs(
    Config config,
    std::vector<BinaryMarket> markets,
    ClobClient* clob,
    OrderChamberRegistry* chambers,
    ArbCallback on_arb,
    StatusCallback on_status)
    : config_(std::move(config)),
      markets_(std::move(markets)),
      clob_(clob),
      chambers_(chambers),
      on_arb_(std::move(on_arb)),
      on_status_(std::move(on_status)) {
  rebuild_indexes();
}

MarketWs::~MarketWs() { stop(); }

void MarketWs::rebuild_indexes() {
  token_to_market_.clear();
  condition_to_market_.clear();
  markets_by_event_.clear();
  books_.clear();
  tokens_with_book_.clear();

  for (const auto& market : markets_) {
    token_to_market_[market.yes_token_id] = market;
    token_to_market_[market.no_token_id] = market;
    condition_to_market_[market.condition_id] = market;
    books_[market.condition_id] = MarketBooks{};
    const std::string event_key = event_key_for_market(market);
    markets_by_event_[event_key].push_back(market);
  }
}

std::string MarketWs::event_key_for_market(const BinaryMarket& market) const {
  return market.event_slug.empty() ? market.condition_id : market.event_slug;
}

void MarketWs::set_markets(const std::vector<BinaryMarket>& markets) {
  std::lock_guard<std::mutex> lock(markets_mutex_);
  markets_ = markets;
  rebuild_indexes();
  markets_dirty_ = true;
  subscribe_pending_ = true;
}

bool MarketWs::push_event(const ws::WsBookEvent& event) {
  while (running_) {
    if (event_ring_.try_push(event)) {
      return true;
    }
    std::this_thread::yield();
  }
  return false;
}

void MarketWs::start() {
  if (running_.exchange(true)) {
    return;
  }
  arb_thread_ = std::thread([this] { arb_worker(); });
  ws_thread_ = std::thread([this] { run(); });
}

void MarketWs::stop() {
  if (!running_.exchange(false)) {
    return;
  }

  while (!event_ring_.try_push(ws::WsBookEvent::shutdown())) {
    std::this_thread::yield();
  }

  if (ws_thread_.joinable()) {
    ws_thread_.join();
  }
  if (arb_thread_.joinable()) {
    arb_thread_.join();
  }
}

bool MarketWs::market_books_ready(const BinaryMarket& market) const {
  return tokens_with_book_.count(market.yes_token_id) > 0 &&
         tokens_with_book_.count(market.no_token_id) > 0;
}

void MarketWs::check_arb_for_market(const BinaryMarket& market) {
  if (!market_books_ready(market)) {
    return;
  }

  const auto tracker = build_game_tracker(config_, markets_, books_, game_tracker_);
  if (!should_scan_for_arb(config_, market, tracker)) {
    return;
  }

  const auto& books = books_.at(market.condition_id);
  if (on_status_) {
    on_status_(market, books);
  }
  if (auto buy = detect_buy_both_arb(
          market, books.yes, books.no, config_.min_net_edge, config_.max_trade_usd)) {
    on_arb_(*buy);
    if (clob_ && chambers_) {
      try_fire_arb(config_, *clob_, *chambers_, *buy);
    }
  }
  if (auto sell = detect_sell_both_arb(
          market, books.yes, books.no, config_.min_net_edge, config_.max_trade_usd)) {
    on_arb_(*sell);
    if (clob_ && chambers_) {
      try_fire_arb(config_, *clob_, *chambers_, *sell);
    }
  }
}

void MarketWs::process_updates_for_event(const std::string& event_slug) {
  const auto event_it = markets_by_event_.find(event_slug);
  if (event_it == markets_by_event_.end()) {
    return;
  }

  build_game_tracker(config_, markets_, books_, game_tracker_);

  for (const auto& market : event_it->second) {
    check_arb_for_market(market);
  }
}

void MarketWs::ingest_message(std::string_view message) {
  if (message == "PONG") {
    return;
  }

  simdjson::dom::element doc;
  if (json_parser_.parse(message).get(doc)) {
    return;
  }

  std::string_view type;
  if (!ws::read_event_type(doc, type)) {
    return;
  }

  if (type == "book") {
    std::string_view token_id;
    if (doc["asset_id"].get(token_id)) {
      return;
    }

    std::string event_key;
    {
      std::lock_guard<std::mutex> lock(markets_mutex_);
      const auto it = token_to_market_.find(std::string(token_id));
      if (it == token_to_market_.end()) {
        return;
      }

      auto& books = books_[it->second.condition_id];
      OrderBook& book =
          (token_id == it->second.yes_token_id) ? books.yes : books.no;
      ws::update_book_from_json(book, doc);
      tokens_with_book_.insert(std::string(token_id));
      event_key = event_key_for_market(it->second);
    }

    (void)push_event(ws::WsBookEvent::dirty(event_key.c_str()));
    return;
  }

  if (type == "price_change") {
    simdjson::dom::array changes;
    if (doc["price_changes"].get(changes)) {
      return;
    }

    for (auto change : changes) {
      ws::WsBookEvent event;
      if (!ws::parse_price_change(change, event)) {
        continue;
      }
      (void)push_event(event);
    }
  }
}

void MarketWs::arb_worker() {
  while (true) {
    ws::WsBookEvent event;
    if (!event_ring_.try_pop(event)) {
      if (!running_ && event_ring_.size_approx() == 0) {
        break;
      }
      std::this_thread::yield();
      continue;
    }

    if (event.kind == ws::WsEventKind::Shutdown) {
      break;
    }

    std::string event_key;
    {
      std::lock_guard<std::mutex> lock(markets_mutex_);
      if (event.kind == ws::WsEventKind::EventDirty) {
        event_key = event.event_key;
      } else if (event.kind == ws::WsEventKind::PriceChange) {
        const auto it = token_to_market_.find(event.token_id);
        if (it == token_to_market_.end()) {
          continue;
        }

        auto& books = books_[it->second.condition_id];
        OrderBook& book =
            (std::string_view(event.token_id) == it->second.yes_token_id) ? books.yes
                                                                          : books.no;
        ws::apply_price_change(book, event.side, event.price, event.size);
        tokens_with_book_.insert(event.token_id);
        event_key = event_key_for_market(it->second);
      }

      if (!event_key.empty()) {
        process_updates_for_event(event_key);
      }
    }
  }
}

std::string MarketWs::build_subscribe_message() const {
  std::lock_guard<std::mutex> lock(markets_mutex_);
  nlohmann::json subscribe = {
      {"type", "market"},
      {"assets_ids", nlohmann::json::array()},
  };
  for (const auto& market : markets_) {
    subscribe["assets_ids"].push_back(market.yes_token_id);
    subscribe["assets_ids"].push_back(market.no_token_id);
  }
  return subscribe.dump();
}

void MarketWs::run() {
  ix::WebSocket ws;
  ws.setUrl(config_.ws_url);

  ws.setOnMessageCallback([this, &ws](const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
      ws_connected_ = true;
      subscribe_pending_ = true;
      if (config_.verbose) {
        std::cout << "WebSocket connected\n";
      }
      return;
    }
    if (msg->type == ix::WebSocketMessageType::Close) {
      ws_connected_ = false;
      if (config_.verbose) {
        std::cerr << "WebSocket disconnected\n";
      }
      return;
    }
    if (msg->type == ix::WebSocketMessageType::Error) {
      std::cerr << "WebSocket error: " << msg->errorInfo.reason << '\n';
      return;
    }
    if (msg->type == ix::WebSocketMessageType::Message) {
      ingest_message(msg->str);
    }
  });

  ws.start();

  auto last_ping = std::chrono::steady_clock::now();
  while (running_) {
    if (ws_connected_ && subscribe_pending_.exchange(false)) {
      ws.send(build_subscribe_message());
      if (config_.verbose) {
        std::lock_guard<std::mutex> lock(markets_mutex_);
        std::cout << "Subscribed to " << (markets_.size() * 2) << " token(s) across "
                  << markets_.size() << " market(s)\n";
      }
    }

    if (markets_dirty_.exchange(false)) {
      subscribe_pending_ = true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_ping >= std::chrono::seconds(10)) {
      if (ws_connected_) {
        ws.send("PING");
      }
      last_ping = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  ws_connected_ = false;
  ws.stop();
}

}  // namespace pm
