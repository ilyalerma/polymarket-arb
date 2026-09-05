#include "pm/order_amounts.hpp"

#include <cmath>
#include <string>

namespace pm {

namespace {

struct RoundConfig {
  int price_digits;
  int size_digits;
  int amount_digits;
};

RoundConfig round_config_for_tick(double tick_size) {
  if (tick_size >= 0.1) {
    return {1, 2, 3};
  }
  if (tick_size >= 0.01) {
    return {2, 2, 4};
  }
  if (tick_size >= 0.001) {
    return {3, 2, 5};
  }
  return {4, 2, 6};
}

double round_down(double value, int digits) {
  const double scale = std::pow(10.0, digits);
  return std::floor(value * scale) / scale;
}

double round_normal(double value, int digits) {
  const double scale = std::pow(10.0, digits);
  return std::round(value * scale) / scale;
}

double round_up(double value, int digits) {
  const double scale = std::pow(10.0, digits);
  return std::ceil(value * scale) / scale;
}

int decimal_places(double value) {
  const auto text = std::to_string(value);
  const auto dot = text.find('.');
  if (dot == std::string::npos) {
    return 0;
  }
  auto end = text.size();
  while (end > dot + 1 && text[end - 1] == '0') {
    --end;
  }
  return static_cast<int>(end - dot - 1);
}

std::uint64_t to_token_decimals(double value) {
  double scaled = value * 1'000'000.0;
  if (decimal_places(scaled) > 0) {
    scaled = round_normal(scaled, 0);
  }
  return static_cast<std::uint64_t>(scaled);
}

double round_collateral(double value, const RoundConfig& config) {
  if (decimal_places(value) > config.amount_digits) {
    value = round_up(value, config.amount_digits + 4);
    if (decimal_places(value) > config.amount_digits) {
      value = round_down(value, config.amount_digits);
    }
  }
  return value;
}

}  // namespace

LegAmountUnits compute_leg_amount_units(
    OrderSide side,
    double size_shares,
    double price,
    double tick_size) {
  const RoundConfig config = round_config_for_tick(tick_size);
  const double raw_price = round_normal(price, config.price_digits);

  LegAmountUnits out;
  if (side == OrderSide::Buy) {
    const double raw_taker = round_down(size_shares, config.size_digits);
    const double raw_maker = round_collateral(raw_taker * raw_price, config);
    out.maker = to_token_decimals(raw_maker);
    out.taker = to_token_decimals(raw_taker);
  } else {
    const double raw_maker = round_down(size_shares, config.size_digits);
    const double raw_taker = round_collateral(raw_maker * raw_price, config);
    out.maker = to_token_decimals(raw_maker);
    out.taker = to_token_decimals(raw_taker);
  }
  return out;
}

OrderAmounts compute_order_amounts(OrderSide side, double shares, double price) {
  const auto units = compute_leg_amount_units(side, shares, price, 0.01);
  OrderAmounts amounts;
  amounts.maker_amount = std::to_string(units.maker);
  amounts.taker_amount = std::to_string(units.taker);
  return amounts;
}

}  // namespace pm
