#pragma once

#include "pm/config.hpp"
#include "pm/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pm {

enum class OrderSide { Buy, Sell };

struct LegBodyBuffer {
  static constexpr std::size_t capacity = 768;
  char data[capacity]{};
  std::size_t size{0};

  std::string_view view() const { return {data, size}; }
};

struct OrderAmounts {
  std::string maker_amount;
  std::string taker_amount;
};

// Test / debug helper. Hot path uses integer units directly.
OrderAmounts compute_order_amounts(OrderSide side, double shares, double price);

struct PreparedLeg {
  void prime(
      const Config& config,
      const std::string& token_id_in,
      OrderSide side_in);
  void aim(double price_in);
  bool ready() const { return priced_ && !head_.empty(); }

  // Hot path: stack buffer only, no heap, no exceptions.
  [[gnu::hot]] bool fire_into(
      LegBodyBuffer& out,
      double quantity_shares,
      std::uint64_t salt,
      std::int64_t timestamp_ms) const noexcept;

 private:
  bool is_buy_{true};
  bool priced_{false};
  std::uint64_t price_units_{0};

  std::string head_;
  std::string mid_salt_;
  std::string mid_maker_;
  std::string mid_taker_;
  std::string tail_;
};

struct FiredShot {
  LegBodyBuffer yes;
  LegBodyBuffer no;
};

struct OrderChamber {
  PreparedLeg yes_leg;
  PreparedLeg no_leg;
  ArbKind kind{ArbKind::BuyBoth};
  std::string market_slug;

  void prime(const BinaryMarket& market, const Config& config, ArbKind kind_in);
  void aim(const ArbOpportunity& opp);
  bool ready() const { return yes_leg.ready() && no_leg.ready(); }

  [[gnu::hot]] bool fire_into(
      FiredShot& out,
      double quantity_shares,
      std::uint64_t salt,
      std::int64_t timestamp_ms) const noexcept;
};

class OrderChamberRegistry {
 public:
  void prime_market(const BinaryMarket& market, const Config& config);
  void prime_all(const std::vector<BinaryMarket>& markets, const Config& config);
  OrderChamber* chamber_for(const std::string& market_slug, ArbKind kind);

 private:
  std::unordered_map<std::string, OrderChamber> buy_chambers_;
  std::unordered_map<std::string, OrderChamber> sell_chambers_;
};

std::uint64_t next_order_salt();
std::int64_t current_timestamp_ms();

}  // namespace pm
