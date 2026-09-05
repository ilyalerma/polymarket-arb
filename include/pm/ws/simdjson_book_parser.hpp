#pragma once

#include "pm/order_book.hpp"
#include "pm/ws/ws_book_event.hpp"

#include <simdjson.h>

#include <string_view>

namespace pm::ws {

bool apply_price_change(OrderBook& book, BookSide side, double price, double size);
bool parse_price_change(simdjson::dom::element change, WsBookEvent& out);
void update_book_from_json(OrderBook& book, simdjson::dom::element payload);
bool read_event_type(simdjson::dom::element doc, std::string_view& type_out);

}  // namespace pm::ws
