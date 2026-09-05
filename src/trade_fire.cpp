#include "pm/trade_fire.hpp"

#include "pm/auth/l2_auth.hpp"

#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace pm {

namespace {

std::string arb_kind_name(ArbKind kind) {
  return kind == ArbKind::BuyBoth ? "BUY_BOTH" : "SELL_BOTH";
}

std::string now_string() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
  return buf;
}

std::string truncate_body(const std::string& body, std::size_t max_len = 240) {
  if (body.size() <= max_len) {
    return body;
  }
  return body.substr(0, max_len) + "...";
}

void log_trade_line(const std::string& message) {
  std::cout << "[" << now_string() << "] TRADE " << message << '\n';
}

const std::string& l2_auth_address(const Config& config) {
  return config.signer_address.empty() ? config.wallet_address : config.signer_address;
}

bool fire_cooldown_active(const std::string& key) {
  static std::mutex mutex;
  static std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_fire;
  static constexpr auto kCooldown = std::chrono::seconds(2);

  const auto now = std::chrono::steady_clock::now();
  std::lock_guard lock(mutex);
  const auto it = last_fire.find(key);
  if (it != last_fire.end() && now - it->second < kCooldown) {
    return true;
  }
  last_fire[key] = now;
  return false;
}

HttpResponse post_leg(
    ClobClient& clob,
    const Config& config,
    const char* leg_name,
    const LegBodyBuffer& body) {
  const auto headers = auth::build_l2_headers(
      l2_auth_address(config),
      config.api_key,
      config.api_secret,
      config.api_passphrase,
      "POST",
      "/order",
      body.data,
      body.size);
  const auto response = clob.post_order_detailed(body.data, body.size, headers);
  if (!response.transport_ok) {
    log_trade_line(std::string(leg_name) + " POST /order transport error");
  } else if (!response.ok()) {
    log_trade_line(
        std::string(leg_name) + " POST /order HTTP " + std::to_string(response.status_code) +
        ": " + truncate_body(response.body));
  } else {
    log_trade_line(
        std::string(leg_name) + " POST /order HTTP " + std::to_string(response.status_code) +
        ": " + truncate_body(response.body));
  }
  return response;
}

}  // namespace

void try_fire_arb(
    const Config& config,
    ClobClient& clob,
    OrderChamberRegistry& chambers,
    const ArbOpportunity& opp) {
  auto* chamber = chambers.chamber_for(opp.market.slug, opp.kind);
  if (!chamber) {
    log_trade_line(
        "skip " + arb_kind_name(opp.kind) + " [" + opp.market.slug + "] — no order chamber");
    return;
  }

  chamber->aim(opp);
  if (!config.live_trading) {
    return;
  }

  const std::string cooldown_key = opp.market.slug + ":" + arb_kind_name(opp.kind);
  if (fire_cooldown_active(cooldown_key)) {
    return;
  }

  if (!chamber->ready()) {
    log_trade_line(
        "skip " + arb_kind_name(opp.kind) + " [" + opp.market.slug +
        "] — chamber not ready after aim");
    return;
  }

  const auto salt = next_order_salt();
  FiredShot shot;
  if (!chamber->fire_into(shot, opp.max_size, salt)) {
    log_trade_line(
        "skip " + arb_kind_name(opp.kind) + " [" + opp.market.slug + "] — fire_into failed");
    return;
  }
  if (!chamber->sign_shot(shot, config, opp.market, salt, opp.max_size)) {
    log_trade_line(
        "skip " + arb_kind_name(opp.kind) + " [" + opp.market.slug + "] — sign_shot failed");
    return;
  }

  log_trade_line(
      "firing " + arb_kind_name(opp.kind) + " [" + opp.market.slug + "] size=" +
      std::to_string(opp.max_size) + " yes@" + std::to_string(opp.yes_price) + " no@" +
      std::to_string(opp.no_price));

  std::thread yes_thread([&] { post_leg(clob, config, "yes", shot.yes); });
  std::thread no_thread([&] { post_leg(clob, config, "no", shot.no); });
  yes_thread.join();
  no_thread.join();
}

bool fire_test_buy(
    const Config& config,
    ClobClient& clob,
    const BinaryMarket& market,
    const bool buy_yes,
    const double size_shares,
    const double limit_price) {
  if (!config.live_trading) {
    log_trade_line("skip test buy — PM_LIVE_TRADING is off");
    return false;
  }

  const auto token_id = buy_yes ? market.yes_token_id : market.no_token_id;
  const auto outcome = buy_yes ? market.yes_outcome : market.no_outcome;
  const auto fee_rate_bps = clob.fetch_fee_rate_bps(token_id);

  PreparedLeg leg;
  leg.prime(config, token_id, OrderSide::Buy, market.tick_size, fee_rate_bps);
  leg.aim(limit_price);

  if (!leg.ready()) {
    log_trade_line("skip test buy — leg not ready");
    return false;
  }

  const auto salt = next_order_salt();
  LegBodyBuffer body;
  if (!leg.fire_into(body, size_shares, salt)) {
    log_trade_line("skip test buy — fire_into failed");
    return false;
  }
  if (!leg.sign_buffer(body, config, market.neg_risk, salt, size_shares)) {
    log_trade_line("skip test buy — sign failed");
    return false;
  }

  log_trade_line(
      "test buy " + outcome + " [" + market.slug + "] size=" + std::to_string(size_shares) +
      " @ " + std::to_string(limit_price) + " (~$" + std::to_string(size_shares * limit_price) +
      ")");

  const auto response = post_leg(clob, config, buy_yes ? "yes" : "no", body);
  return response.ok();
}

}  // namespace pm
