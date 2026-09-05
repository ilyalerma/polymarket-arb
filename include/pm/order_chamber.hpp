#pragma once

#include "pm/config.hpp"
#include "pm/order_amounts.hpp"
#include "pm/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pm {

class ClobClient;

struct LegBodyBuffer {
  static constexpr std::size_t capacity = 768;
  char data[capacity]{};
  std::size_t size{0};

  std::string_view view() const { return {data, size}; }
};

struct PreparedLeg {
  void prime(
      const Config& config,
      const std::string& token_id_in,
      OrderSide side_in,
      double tick_size_in,
      std::uint32_t fee_rate_bps_in);
  void aim(double price_in);
  bool ready() const { return priced_ && !head_.empty(); }

  [[gnu::hot]] bool fire_into(
      LegBodyBuffer& out,
      double quantity_shares,
      std::uint64_t salt) const noexcept;

  bool sign_buffer(
      LegBodyBuffer& buffer,
      const Config& config,
      bool neg_risk,
      std::uint64_t salt,
      double quantity_shares) const;

 private:
  LegAmountUnits amount_units(double quantity_shares) const;

  bool is_buy_{true};
  bool priced_{false};
  double price_{0.0};
  double tick_size_{0.01};
  std::uint32_t fee_rate_bps_{0};
  std::uint8_t signature_type_{0};
  std::string token_id_;
  std::string maker_address_;
  std::string signer_address_;
  std::string owner_;

  std::string head_;
  std::string mid_after_salt_;
  std::string mid_before_fee_;
  std::string mid_after_fee_;
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

  void prime(
      const BinaryMarket& market,
      const Config& config,
      ArbKind kind_in,
      const ClobClient* clob = nullptr);
  void aim(const ArbOpportunity& opp);
  bool ready() const { return yes_leg.ready() && no_leg.ready(); }

  [[gnu::hot]] bool fire_into(
      FiredShot& out,
      double quantity_shares,
      std::uint64_t salt) const noexcept;

  bool sign_shot(
      FiredShot& out,
      const Config& config,
      const BinaryMarket& market,
      std::uint64_t salt,
      double quantity_shares) const;
};

class OrderChamberRegistry {
 public:
  void prime_market(
      const BinaryMarket& market,
      const Config& config,
      const ClobClient* clob = nullptr);
  void prime_all(
      const std::vector<BinaryMarket>& markets,
      const Config& config,
      const ClobClient* clob = nullptr);
  OrderChamber* chamber_for(const std::string& market_slug, ArbKind kind);

 private:
  std::unordered_map<std::string, OrderChamber> buy_chambers_;
  std::unordered_map<std::string, OrderChamber> sell_chambers_;
};

std::uint64_t next_order_salt();
std::int64_t current_timestamp_ms();

}  // namespace pm
