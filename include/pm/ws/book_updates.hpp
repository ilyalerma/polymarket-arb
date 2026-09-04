#pragma once

#include "pm/order_book.hpp"

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace pm::ws {

double parse_decimal_field(const nlohmann::json& obj, const char* key);
void update_book_from_payload(OrderBook& book, const nlohmann::json& payload);
bool apply_price_change(OrderBook& book, const nlohmann::json& change);

}  // namespace pm::ws
