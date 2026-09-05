#include "pm/ws/book_updates.hpp"
#include "pm/ws/simdjson_book_parser.hpp"

#include <nlohmann/json.hpp>

namespace pm::ws {

double parse_decimal_field(const nlohmann::json& obj, const char* key) {
  const auto& value = obj.at(key);
  if (value.is_string()) {
    return std::stod(value.get<std::string>());
  }
  return value.get<double>();
}

void update_book_from_payload(OrderBook& book, const nlohmann::json& payload) {
  TokenBook token_book;
  token_book.token_id = payload.value("asset_id", payload.value("tokenId", ""));
  if (payload.contains("bids")) {
    for (const auto& level : payload.at("bids")) {
      token_book.bids.push_back(
          {parse_decimal_field(level, "price"), parse_decimal_field(level, "size")});
    }
  }
  if (payload.contains("asks")) {
    for (const auto& level : payload.at("asks")) {
      token_book.asks.push_back(
          {parse_decimal_field(level, "price"), parse_decimal_field(level, "size")});
    }
  }
  if (payload.contains("tick_size")) {
    token_book.tick_size = parse_decimal_field(payload, "tick_size");
  }
  if (payload.contains("min_order_size")) {
    token_book.min_order_size = parse_decimal_field(payload, "min_order_size");
  }
  book.update(token_book);
}

bool apply_price_change(OrderBook& book, const nlohmann::json& change) {
  if (!change.contains("side") || !change.contains("price") || !change.contains("size")) {
    return false;
  }

  const std::string side = change.at("side").get<std::string>();
  const double price = parse_decimal_field(change, "price");
  const double size = parse_decimal_field(change, "size");
  if (side == "BUY") {
    return apply_price_change(book, BookSide::Bid, price, size);
  }
  if (side == "SELL") {
    return apply_price_change(book, BookSide::Ask, price, size);
  }
  return false;
}

}  // namespace pm::ws
