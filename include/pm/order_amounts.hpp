#pragma once

#include <cstdint>
#include <string>

namespace pm {

enum class OrderSide { Buy, Sell };

struct OrderAmounts {
  std::string maker_amount;
  std::string taker_amount;
};

struct LegAmountUnits {
  std::uint64_t maker{0};
  std::uint64_t taker{0};
};

LegAmountUnits compute_leg_amount_units(
    OrderSide side,
    double size_shares,
    double price,
    double tick_size);

OrderAmounts compute_order_amounts(OrderSide side, double shares, double price);

}  // namespace pm
