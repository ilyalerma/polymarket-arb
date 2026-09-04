#pragma once

#include <cstdint>
#include <string>

namespace pm {

struct Config {
  std::string gamma_url{"https://gamma-api.polymarket.com"};
  std::string clob_url{"https://clob.polymarket.com"};
  std::string ws_url{"wss://ws-subscriptions-clob.polymarket.com/ws/market"};

  bool live_trading{false};
  std::string private_key;
  std::string api_key;
  std::string api_secret;
  std::string api_passphrase;
  std::string wallet_address;

  double min_net_edge{0.002};
  double max_trade_usd{100.0};
  std::uint32_t market_limit{200};
  std::uint32_t poll_interval_ms{2000};
  double min_liquidity{5000.0};
  bool use_websocket{true};
  std::string market_slug;
  bool watch_status{false};
  bool verbose{false};
  bool benchmark_mode{false};
  std::uint32_t benchmark_iterations{10};
};

Config load_config_from_env();

}  // namespace pm
