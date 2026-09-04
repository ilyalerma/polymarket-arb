#include "pm/ws/market_ws.hpp"

#include "pm/auth/l2_auth.hpp"
#include "pm/fee_model.hpp"
#include "pm/order_chamber.hpp"
#include "pm/ws/book_updates.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <unordered_set>

namespace pm {

namespace {

void try_fire_arb(
    const Config& config,
    ClobClient* clob,
    OrderChamberRegistry* chambers,
    const ArbOpportunity& opp) {
  if (!clob || !chambers) {
    return;
  }

  auto* chamber = chambers->chamber_for(opp.market.slug, opp.kind);
  if (!chamber) {
    return;
  }

  chamber->aim(opp);
  if (!config.live_trading) {
    return;
  }

  const auto salt = next_order_salt();
  const auto timestamp_ms = current_timestamp_ms();
  FiredShot shot;
  if (!chamber->fire_into(shot, opp.max_size, salt, timestamp_ms)) {
    return;
  }

  const auto post_leg = [&](const LegBodyBuffer& body) {
    const auto headers = auth::build_l2_headers(
        config.wallet_address,
        config.api_key,
        config.api_secret,
        config.api_passphrase,
        "POST",
        "/order",
        body.data,
        body.size);
    (void)clob->post_order(body.data, body.size, headers);
  };

  post_leg(shot.yes);
  post_leg(shot.no);
}

}  // namespace

MarketWs::MarketWs(
    Config config,
    std::vector<BinaryMarket> markets,
    ClobClient* clob,
    OrderChamberRegistry* chambers,
    ArbCallback on_arb)
    : config_(std::move(config)),
      markets_(std::move(markets)),
      clob_(clob),
      chambers_(chambers),
      on_arb_(std::move(on_arb)) {
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
    const std::string event_key =
        market.event_slug.empty() ? market.condition_id : market.event_slug;
    markets_by_event_[event_key].push_back(market);
  }
}

void MarketWs::set_markets(const std::vector<BinaryMarket>& markets) {
  std::lock_guard<std::mutex> lock(markets_mutex_);
  markets_ = markets;
  rebuild_indexes();
  markets_dirty_ = true;
}

void MarketWs::start() {
  if (running_.exchange(true)) {
    return;
  }
  thread_ = std::thread([this] { run(); });
}

void MarketWs::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  if (thread_.joinable()) {
    thread_.join();
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
  if (auto buy = detect_buy_both_arb(
          market, books.yes, books.no, config_.min_net_edge, config_.max_trade_usd)) {
    on_arb_(*buy);
    try_fire_arb(config_, clob_, chambers_, *buy);
  }
  if (auto sell = detect_sell_both_arb(
          market, books.yes, books.no, config_.min_net_edge, config_.max_trade_usd)) {
    on_arb_(*sell);
    try_fire_arb(config_, clob_, chambers_, *sell);
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

void MarketWs::handle_message(const std::string& message) {
  if (message == "PONG") {
    return;
  }

  nlohmann::json event;
  try {
    event = nlohmann::json::parse(message);
  } catch (...) {
    return;
  }

  std::lock_guard<std::mutex> lock(markets_mutex_);

  const std::string type = event.value("event_type", event.value("type", ""));
  if (type == "book") {
    const auto token_id = event.value("asset_id", "");
    const auto it = token_to_market_.find(token_id);
    if (it == token_to_market_.end()) {
      return;
    }

    auto& books = books_[it->second.condition_id];
    if (token_id == it->second.yes_token_id) {
      ws::update_book_from_payload(books.yes, event);
    } else {
      ws::update_book_from_payload(books.no, event);
    }
    tokens_with_book_.insert(token_id);

    const std::string event_key = it->second.event_slug.empty() ? it->second.condition_id
                                                                : it->second.event_slug;
    process_updates_for_event(event_key);
    return;
  }

  if (type == "price_change" && event.contains("price_changes")) {
    std::unordered_set<std::string> touched_events;
    for (const auto& change : event.at("price_changes")) {
      const auto token_id = change.value("asset_id", "");
      const auto it = token_to_market_.find(token_id);
      if (it == token_to_market_.end()) {
        continue;
      }

      auto& books = books_[it->second.condition_id];
      OrderBook* book = nullptr;
      if (token_id == it->second.yes_token_id) {
        book = &books.yes;
      } else if (token_id == it->second.no_token_id) {
        book = &books.no;
      }
      if (!book || !ws::apply_price_change(*book, change)) {
        continue;
      }

      tokens_with_book_.insert(token_id);
      const std::string event_key = it->second.event_slug.empty() ? it->second.condition_id
                                                                  : it->second.event_slug;
      touched_events.insert(event_key);
    }

    for (const auto& event_key : touched_events) {
      process_updates_for_event(event_key);
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

  ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Message) {
      handle_message(msg->str);
    }
  });

  ws.start();
  ws.send(build_subscribe_message());

  auto last_ping = std::chrono::steady_clock::now();
  while (running_) {
    if (markets_dirty_.exchange(false)) {
      ws.send(build_subscribe_message());
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_ping >= std::chrono::seconds(10)) {
      ws.send("PING");
      last_ping = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  ws.stop();
}

}  // namespace pm
