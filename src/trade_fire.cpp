#include "pm/trade_fire.hpp"

#include "pm/auth/l2_auth.hpp"

#include <thread>

namespace pm {

void try_fire_arb(
    const Config& config,
    ClobClient& clob,
    OrderChamberRegistry& chambers,
    const ArbOpportunity& opp) {
  auto* chamber = chambers.chamber_for(opp.market.slug, opp.kind);
  if (!chamber) {
    return;
  }

  chamber->aim(opp);
  if (!config.live_trading) {
    return;
  }

  const auto salt = next_order_salt();
  const auto timestamp_ms = current_timestamp_ms();
  FiredShot shot;
  if (!chamber->fire_into(shot, opp.max_size, salt, timestamp_ms)) {
    return;
  }
  if (!chamber->sign_shot(shot, config, opp.market, salt, timestamp_ms, opp.max_size)) {
    return;
  }

  const auto post_leg = [&](const LegBodyBuffer& body) {
    const auto headers = auth::build_l2_headers(
        config.wallet_address,
        config.api_key,
        config.api_secret,
        config.api_passphrase,
        "POST",
        "/order",
        body.data,
        body.size);
    (void)clob.post_order(body.data, body.size, headers);
  };

  std::thread yes_thread(post_leg, shot.yes);
  std::thread no_thread(post_leg, shot.no);
  yes_thread.join();
  no_thread.join();
}

}  // namespace pm
