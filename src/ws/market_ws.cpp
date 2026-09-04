#include "pm/ws/market_ws.hpp"

#include "pm/clob_client.hpp"
#include "pm/fee_model.hpp"
#include "pm/http_client.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include <iostream>

namespace pm {

namespace {

using json = nlohmann::json;

void update_book_from_payload(OrderBook& book, const json& payload) {
  TokenBook token_book;
  token_book.token_id = payload.value("asset_id", payload.value("tokenId", ""));
  if (payload.contains("bids")) {
    for (const auto& level : payload.at("bids")) {
      token_book.bids.push_back(
          {std::stod(level.at("price").get<std::string>()),
           std::stod(level.at("size").get<std::string>())});
    }
  }
  if (payload.contains("asks")) {
    for (const auto& level : payload.at("asks")) {
      token_book.asks.push_back(
          {std::stod(level.at("price").get<std::string>()),
           std::stod(level.at("size").get<std::string>())});
    }
  }
  if (payload.contains("tick_size")) {
    token_book.tick_size = std::stod(payload.at("tick_size").get<std::string>());
  }
  if (payload.contains("min_order_size")) {
    token_book.min_order_size =
        std::stod(payload.at("min_order_size").get<std::string>());
  }
  book.update(token_book);
}

}  // namespace

MarketWs::MarketWs(
    Config config,
    std::vector<BinaryMarket> markets,
    ArbCallback on_arb)
    : config_(std::move(config)),
      markets_(std::move(markets)),
      on_arb_(std::move(on_arb)) {
  for (const auto& market : markets_) {
    token_to_market_[market.yes_token_id] = market;
    token_to_market_[market.no_token_id] = market;
    books_[market.condition_id] = MarketBooks{};
  }
}

MarketWs::~MarketWs() { stop(); }

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

void MarketWs::refresh_books_for_market(const BinaryMarket& market) {
  ClobClient clob(HttpClient(config_.clob_url));
  const auto yes = clob.fetch_book(market.yes_token_id);
  const auto no = clob.fetch_book(market.no_token_id);
  if (!yes || !no) {
    return;
  }
  auto& books = books_[market.condition_id];
  books.yes.update(*yes);
  books.no.update(*no);

  if (auto buy = detect_buy_both_arb(
          market, books.yes, books.no, config_.min_net_edge, config_.max_trade_usd)) {
    on_arb_(*buy);
  }
  if (auto sell = detect_sell_both_arb(
          market, books.yes, books.no, config_.min_net_edge, config_.max_trade_usd)) {
    on_arb_(*sell);
  }
}

void MarketWs::handle_message(const std::string& message) {
  if (message == "PONG") {
    return;
  }

  json event;
  try {
    event = json::parse(message);
  } catch (...) {
    return;
  }

  const std::string type = event.value("event_type", event.value("type", ""));
  if (type == "book") {
    const auto token_id = event.value("asset_id", "");
    const auto it = token_to_market_.find(token_id);
    if (it == token_to_market_.end()) {
      return;
    }
    auto& books = books_[it->second.condition_id];
    if (token_id == it->second.yes_token_id) {
      update_book_from_payload(books.yes, event);
    } else {
      update_book_from_payload(books.no, event);
    }
    if (auto buy = detect_buy_both_arb(
            it->second, books.yes, books.no, config_.min_net_edge, config_.max_trade_usd)) {
      on_arb_(*buy);
    }
    if (auto sell = detect_sell_both_arb(
            it->second, books.yes, books.no, config_.min_net_edge, config_.max_trade_usd)) {
      on_arb_(*sell);
    }
    return;
  }

  if (type == "price_change" && event.contains("price_changes")) {
    for (const auto& change : event.at("price_changes")) {
      const auto token_id = change.value("asset_id", "");
      const auto it = token_to_market_.find(token_id);
      if (it == token_to_market_.end()) {
        continue;
      }
      refresh_books_for_market(it->second);
    }
  }
}

void MarketWs::run() {
  for (const auto& market : markets_) {
    refresh_books_for_market(market);
  }

  ix::WebSocket ws;
  ws.setUrl(config_.ws_url);

  json subscribe = {
      {"type", "market"},
      {"assets_ids", json::array()},
  };
  for (const auto& market : markets_) {
    subscribe["assets_ids"].push_back(market.yes_token_id);
    subscribe["assets_ids"].push_back(market.no_token_id);
  }

  ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Message) {
      handle_message(msg->str);
    }
  });

  ws.start();
  ws.send(subscribe.dump());

  while (running_) {
    ws.send("PING");
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }

  ws.stop();
}

}  // namespace pm
