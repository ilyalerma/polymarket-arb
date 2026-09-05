#include "pm/ws/simdjson_book_parser.hpp"

#include "pm/fast_decimal.hpp"

namespace pm::ws {

namespace {

bool parse_book_side(std::string_view side, BookSide& out) {
  if (side == "BUY") {
    out = BookSide::Bid;
    return true;
  }
  if (side == "SELL") {
    out = BookSide::Ask;
    return true;
  }
  return false;
}

bool parse_level(simdjson::dom::element level, BookLevel& out) {
  std::string_view price_text;
  std::string_view size_text;
  if (level["price"].get(price_text) || level["size"].get(size_text)) {
    return false;
  }
  return parse_double(price_text, out.price) && parse_double(size_text, out.size);
}

}  // namespace

bool apply_price_change(OrderBook& book, BookSide side, double price, double size) {
  book.apply_level(side, price, size);
  return true;
}

bool parse_price_change(simdjson::dom::element change, WsBookEvent& out) {
  std::string_view token_id;
  std::string_view side_text;
  std::string_view price_text;
  std::string_view size_text;
  if (change["asset_id"].get(token_id) || change["side"].get(side_text) ||
      change["price"].get(price_text) || change["size"].get(size_text)) {
    return false;
  }

  BookSide side{};
  if (!parse_book_side(side_text, side)) {
    return false;
  }

  double price = 0.0;
  double size = 0.0;
  if (!parse_double(price_text, price) || !parse_double(size_text, size)) {
    return false;
  }

  out.kind = WsEventKind::PriceChange;
  WsBookEvent::copy_string(out.token_id, token_id);
  out.side = side;
  out.price = price;
  out.size = size;
  return true;
}

void update_book_from_json(OrderBook& book, simdjson::dom::element payload) {
  TokenBook token_book;

  simdjson::dom::array bids;
  if (!payload["bids"].get(bids)) {
    for (auto level : bids) {
      BookLevel parsed;
      if (parse_level(level, parsed)) {
        token_book.bids.push_back(parsed);
      }
    }
  }

  simdjson::dom::array asks;
  if (!payload["asks"].get(asks)) {
    for (auto level : asks) {
      BookLevel parsed;
      if (parse_level(level, parsed)) {
        token_book.asks.push_back(parsed);
      }
    }
  }

  std::string_view tick_size_text;
  if (!payload["tick_size"].get(tick_size_text)) {
    parse_double(tick_size_text, token_book.tick_size);
  }

  std::string_view min_order_size_text;
  if (!payload["min_order_size"].get(min_order_size_text)) {
    parse_double(min_order_size_text, token_book.min_order_size);
  }

  book.update(token_book);
}

bool read_event_type(simdjson::dom::element doc, std::string_view& type_out) {
  if (!doc["event_type"].get(type_out)) {
    return true;
  }
  return !doc["type"].get(type_out);
}

}  // namespace pm::ws
