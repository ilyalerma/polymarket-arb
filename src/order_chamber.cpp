#include "pm/order_chamber.hpp"

#include "pm/fast_format.hpp"

#include <atomic>
#include <chrono>
#include <cmath>

namespace pm {

namespace {

constexpr double kAmountScale = 1'000'000.0;
constexpr std::uint64_t kHalfScale = 500'000;

std::string resolve_owner(const Config& config) {
  if (!config.api_key.empty()) {
    return config.api_key;
  }
  return "dry-run";
}

std::uint64_t to_share_units(double shares) noexcept {
  return static_cast<std::uint64_t>(std::llround(shares * kAmountScale));
}

std::uint64_t to_price_units(double price) noexcept {
  return static_cast<std::uint64_t>(std::llround(price * kAmountScale));
}

std::uint64_t collateral_units(std::uint64_t share_units, std::uint64_t price_units) noexcept {
  return (share_units * price_units + kHalfScale) / static_cast<std::uint64_t>(kAmountScale);
}

}  // namespace

OrderAmounts compute_order_amounts(OrderSide side, double shares, double price) {
  const auto share_units = to_share_units(shares);
  const auto price_units = to_price_units(price);
  const auto collateral = collateral_units(share_units, price_units);

  OrderAmounts amounts;
  if (side == OrderSide::Buy) {
    amounts.maker_amount = std::to_string(collateral);
    amounts.taker_amount = std::to_string(share_units);
  } else {
    amounts.maker_amount = std::to_string(share_units);
    amounts.taker_amount = std::to_string(collateral);
  }
  return amounts;
}

void PreparedLeg::prime(
    const Config& config,
    const std::string& token_id_in,
    OrderSide side_in) {
  is_buy_ = side_in == OrderSide::Buy;
  priced_ = false;
  price_units_ = 0;

  const std::string maker = config.wallet_address.empty()
                                ? "0x0000000000000000000000000000000000000001"
                                : config.wallet_address;
  const std::string metadata =
      "0x0000000000000000000000000000000000000000000000000000000000000000";
  const std::string builder =
      "0x0000000000000000000000000000000000000000000000000000000000000000";
  const std::string owner = resolve_owner(config);

  head_ = R"({"order":{"salt":")";
  mid_salt_ = R"(","maker":")";
  mid_salt_ += maker;
  mid_salt_ += R"(","signer":")";
  mid_salt_ += maker;
  mid_salt_ += R"(","tokenId":")";
  mid_salt_ += token_id_in;
  mid_salt_ += R"(","makerAmount":")";

  mid_maker_ = R"(","takerAmount":")";

  mid_taker_ = R"(","side":")";
  mid_taker_ += is_buy_ ? "BUY" : "SELL";
  mid_taker_ += R"(","signatureType":0,"timestamp":")";

  tail_ = R"(","metadata":")";
  tail_ += metadata;
  tail_ += R"(","builder":")";
  tail_ += builder;
  tail_ += R"(","signature":"0x"},"owner":")";
  tail_ += owner;
  tail_ += R"(","orderType":"FOK","postOnly":false})";
}

void PreparedLeg::aim(double price_in) {
  price_units_ = to_price_units(price_in);
  priced_ = price_units_ > 0;
}

bool PreparedLeg::fire_into(
    LegBodyBuffer& out,
    double quantity_shares,
    std::uint64_t salt,
    std::int64_t timestamp_ms) const noexcept {
  if (!ready() || quantity_shares <= 0.0) {
    return false;
  }

  const std::uint64_t share_units = to_share_units(quantity_shares);
  const std::uint64_t maker_units =
      is_buy_ ? collateral_units(share_units, price_units_) : share_units;
  const std::uint64_t taker_units =
      is_buy_ ? share_units : collateral_units(share_units, price_units_);

  char* cursor = out.data;
  const char* const end = out.data + LegBodyBuffer::capacity;

  cursor = fast::append(cursor, head_);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::write_u64(cursor, salt);
  cursor = fast::append(cursor, mid_salt_);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::write_u64(cursor, maker_units);
  cursor = fast::append(cursor, mid_maker_);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::write_u64(cursor, taker_units);
  cursor = fast::append(cursor, mid_taker_);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::write_i64(cursor, timestamp_ms);
  cursor = fast::append(cursor, tail_);
  if (cursor > end) {
    return false;
  }

  out.size = static_cast<std::size_t>(cursor - out.data);
  return true;
}

void OrderChamber::prime(const BinaryMarket& market, const Config& config, ArbKind kind_in) {
  market_slug = market.slug;
  kind = kind_in;

  if (kind == ArbKind::BuyBoth) {
    yes_leg.prime(config, market.yes_token_id, OrderSide::Buy);
    no_leg.prime(config, market.no_token_id, OrderSide::Buy);
  } else {
    yes_leg.prime(config, market.yes_token_id, OrderSide::Sell);
    no_leg.prime(config, market.no_token_id, OrderSide::Sell);
  }
}

void OrderChamber::aim(const ArbOpportunity& opp) {
  yes_leg.aim(opp.yes_price);
  no_leg.aim(opp.no_price);
}

bool OrderChamber::fire_into(
    FiredShot& out,
    double quantity_shares,
    std::uint64_t salt,
    std::int64_t timestamp_ms) const noexcept {
  if (!ready()) {
    return false;
  }
  if (!yes_leg.fire_into(out.yes, quantity_shares, salt, timestamp_ms)) {
    return false;
  }
  return no_leg.fire_into(out.no, quantity_shares, salt + 1, timestamp_ms);
}

void OrderChamberRegistry::prime_market(const BinaryMarket& market, const Config& config) {
  buy_chambers_[market.slug].prime(market, config, ArbKind::BuyBoth);
  sell_chambers_[market.slug].prime(market, config, ArbKind::SellBoth);
}

void OrderChamberRegistry::prime_all(
    const std::vector<BinaryMarket>& markets,
    const Config& config) {
  for (const auto& market : markets) {
    prime_market(market, config);
  }
}

OrderChamber* OrderChamberRegistry::chamber_for(
    const std::string& market_slug,
    ArbKind kind) {
  if (kind == ArbKind::BuyBoth) {
    const auto it = buy_chambers_.find(market_slug);
    return it == buy_chambers_.end() ? nullptr : &it->second;
  }
  const auto it = sell_chambers_.find(market_slug);
  return it == sell_chambers_.end() ? nullptr : &it->second;
}

std::uint64_t next_order_salt() {
  static std::atomic<std::uint64_t> counter{1};
  const auto tick = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  return counter.fetch_add(1, std::memory_order_relaxed) ^ tick;
}

std::int64_t current_timestamp_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace pm
