#include "pm/trade_fire.hpp"

#include "pm/auth/l2_auth.hpp"

#include <chrono>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

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

  if (!chamber->ready()) {
    log_trade_line(
        "skip " + arb_kind_name(opp.kind) + " [" + opp.market.slug +
        "] — chamber not ready after aim");
    return;
  }

  const auto salt = next_order_salt();
  const auto timestamp_ms = current_timestamp_ms();
  FiredShot shot;
  if (!chamber->fire_into(shot, opp.max_size, salt, timestamp_ms)) {
    log_trade_line(
        "skip " + arb_kind_name(opp.kind) + " [" + opp.market.slug + "] — fire_into failed");
    return;
  }
  if (!chamber->sign_shot(shot, config, opp.market, salt, timestamp_ms, opp.max_size)) {
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

}  // namespace pm
