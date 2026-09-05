#include "pm/order_chamber.hpp"

#include "pm/auth/eip712_sign.hpp"
#include "pm/clob_client.hpp"
#include "pm/fast_format.hpp"
#include "pm/order_amounts.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

namespace pm {

namespace {

constexpr char kZeroAddress[] = "0x0000000000000000000000000000000000000000";
constexpr char kSignaturePlaceholder[] =
    "0x000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000";

std::string resolve_owner(const Config& config) {
  if (!config.api_key.empty()) {
    return config.api_key;
  }
  return "dry-run";
}

std::uint32_t resolve_fee_rate_bps(const ClobClient* clob, const std::string& token_id) {
  if (clob == nullptr) {
    return 0;
  }
  return clob->fetch_fee_rate_bps(token_id);
}

bool patch_signature(LegBodyBuffer& buffer, const std::string& signature) {
  constexpr char marker[] = R"("signature":")";
  if (signature.size() != 132) {
    return false;
  }
  char* start = std::strstr(buffer.data, marker);
  if (!start) {
    return false;
  }
  char* sig_value = start + sizeof(marker) - 1;
  std::memcpy(sig_value, signature.c_str(), 132);
  return true;
}

}  // namespace

void PreparedLeg::prime(
    const Config& config,
    const std::string& token_id_in,
    OrderSide side_in,
    double tick_size_in,
    std::uint32_t fee_rate_bps_in) {
  is_buy_ = side_in == OrderSide::Buy;
  priced_ = false;
  price_ = 0.0;
  tick_size_ = tick_size_in > 0.0 ? tick_size_in : 0.01;
  fee_rate_bps_ = fee_rate_bps_in;

  const std::string maker = config.wallet_address.empty()
                                ? "0x0000000000000000000000000000000000000001"
                                : config.wallet_address;
  const std::string signer = config.signer_address.empty() ? maker : config.signer_address;

  token_id_ = token_id_in;
  signature_type_ = config.signature_type;
  maker_address_ = maker;
  signer_address_ = signer;
  owner_ = resolve_owner(config);

  head_ = R"({"order":{"salt":")";
  mid_after_salt_ = R"(","maker":")";
  mid_after_salt_ += maker;
  mid_after_salt_ += R"(","signer":")";
  mid_after_salt_ += signer;
  mid_after_salt_ += R"(","taker":")";
  mid_after_salt_ += kZeroAddress;
  mid_after_salt_ += R"(","tokenId":")";
  mid_after_salt_ += token_id_in;
  mid_after_salt_ += R"(","makerAmount":")";

  mid_before_fee_ = R"(","takerAmount":")";

  mid_after_fee_ = R"(","expiration":"0","nonce":"0","feeRateBps":")";

  tail_ = R"("},"owner":")";
  tail_ += owner_;
  tail_ += R"(","orderType":"FOK","postOnly":false})";
}

void PreparedLeg::aim(double price_in) {
  price_ = price_in;
  priced_ = price_in > 0.0;
}

LegAmountUnits PreparedLeg::amount_units(double quantity_shares) const {
  return compute_leg_amount_units(
      is_buy_ ? OrderSide::Buy : OrderSide::Sell,
      quantity_shares,
      price_,
      tick_size_);
}

bool PreparedLeg::fire_into(
    LegBodyBuffer& out,
    double quantity_shares,
    std::uint64_t salt) const noexcept {
  if (!ready() || quantity_shares <= 0.0) {
    return false;
  }

  const auto amounts = amount_units(quantity_shares);
  char* cursor = out.data;
  const char* const end = out.data + LegBodyBuffer::capacity;

  cursor = fast::append(cursor, head_);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::write_u64(cursor, salt);
  cursor = fast::append(cursor, mid_after_salt_);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::write_u64(cursor, amounts.maker);
  cursor = fast::append(cursor, mid_before_fee_);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::write_u64(cursor, amounts.taker);
  cursor = fast::append(cursor, mid_after_fee_);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::write_u64(cursor, fee_rate_bps_);
  cursor = fast::append(cursor, R"(","side":")");
  cursor = fast::append(cursor, is_buy_ ? "BUY" : "SELL");
  cursor = fast::append(cursor, R"(","signatureType":)");
  cursor = fast::write_u64(cursor, signature_type_);
  cursor = fast::append(cursor, R"(,"signature":")");
  cursor = fast::append(cursor, kSignaturePlaceholder);
  if (cursor >= end) {
    return false;
  }
  cursor = fast::append(cursor, tail_);
  if (cursor > end) {
    return false;
  }

  out.size = static_cast<std::size_t>(cursor - out.data);
  return true;
}

bool PreparedLeg::sign_buffer(
    LegBodyBuffer& buffer,
    const Config& config,
    bool neg_risk,
    std::uint64_t salt,
    double quantity_shares) const {
  if (!config.live_trading || config.private_key.empty()) {
    return true;
  }

  const auto amounts = amount_units(quantity_shares);

  auth::OrderSignFields fields;
  fields.salt = salt;
  fields.maker = maker_address_;
  fields.signer = signer_address_;
  fields.taker = kZeroAddress;
  fields.token_id = token_id_;
  fields.maker_amount = amounts.maker;
  fields.taker_amount = amounts.taker;
  fields.expiration = 0;
  fields.nonce = 0;
  fields.fee_rate_bps = fee_rate_bps_;
  fields.side = is_buy_ ? 0 : 1;
  fields.signature_type = signature_type_;

  try {
    const std::string signature =
        auth::sign_polymarket_order(fields, neg_risk, config.private_key);
    return patch_signature(buffer, signature);
  } catch (...) {
    return false;
  }
}

void OrderChamber::prime(
    const BinaryMarket& market,
    const Config& config,
    ArbKind kind_in,
    const ClobClient* clob) {
  market_slug = market.slug;
  kind = kind_in;

  const auto yes_fee = resolve_fee_rate_bps(clob, market.yes_token_id);
  const auto no_fee = resolve_fee_rate_bps(clob, market.no_token_id);

  if (kind == ArbKind::BuyBoth) {
    yes_leg.prime(config, market.yes_token_id, OrderSide::Buy, market.tick_size, yes_fee);
    no_leg.prime(config, market.no_token_id, OrderSide::Buy, market.tick_size, no_fee);
  } else {
    yes_leg.prime(config, market.yes_token_id, OrderSide::Sell, market.tick_size, yes_fee);
    no_leg.prime(config, market.no_token_id, OrderSide::Sell, market.tick_size, no_fee);
  }
}

void OrderChamber::aim(const ArbOpportunity& opp) {
  yes_leg.aim(opp.yes_price);
  no_leg.aim(opp.no_price);
}

bool OrderChamber::fire_into(
    FiredShot& out,
    double quantity_shares,
    std::uint64_t salt) const noexcept {
  if (!ready()) {
    return false;
  }
  if (!yes_leg.fire_into(out.yes, quantity_shares, salt)) {
    return false;
  }
  return no_leg.fire_into(out.no, quantity_shares, salt + 1);
}

bool OrderChamber::sign_shot(
    FiredShot& out,
    const Config& config,
    const BinaryMarket& market,
    std::uint64_t salt,
    double quantity_shares) const {
  if (!yes_leg.sign_buffer(out.yes, config, market.neg_risk, salt, quantity_shares)) {
    return false;
  }
  return no_leg.sign_buffer(out.no, config, market.neg_risk, salt + 1, quantity_shares);
}

void OrderChamberRegistry::prime_market(
    const BinaryMarket& market,
    const Config& config,
    const ClobClient* clob) {
  buy_chambers_[market.slug].prime(market, config, ArbKind::BuyBoth, clob);
  sell_chambers_[market.slug].prime(market, config, ArbKind::SellBoth, clob);
}

void OrderChamberRegistry::prime_all(
    const std::vector<BinaryMarket>& markets,
    const Config& config,
    const ClobClient* clob) {
  for (const auto& market : markets) {
    prime_market(market, config, clob);
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
