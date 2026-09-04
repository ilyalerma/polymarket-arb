#include "pm/config.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace pm {

namespace {

bool env_truthy(const char* value) {
  if (value == nullptr) {
    return false;
  }
  return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
         std::strcmp(value, "TRUE") == 0;
}

double env_double(const char* name, double fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return fallback;
  }
  return std::stod(value);
}

std::uint32_t env_uint(const char* name, std::uint32_t fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return fallback;
  }
  return static_cast<std::uint32_t>(std::stoul(value));
}

std::string env_string(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string{value};
}

}  // namespace

Config load_config_from_env() {
  Config config;
  config.gamma_url = env_string("PM_GAMMA_URL").empty() ? config.gamma_url : env_string("PM_GAMMA_URL");
  config.clob_url = env_string("PM_CLOB_URL").empty() ? config.clob_url : env_string("PM_CLOB_URL");
  config.ws_url = env_string("PM_WS_URL").empty() ? config.ws_url : env_string("PM_WS_URL");

  config.live_trading = env_truthy(std::getenv("PM_LIVE_TRADING"));
  config.private_key = env_string("PM_PRIVATE_KEY");
  config.api_key = env_string("PM_API_KEY");
  config.api_secret = env_string("PM_API_SECRET");
  config.api_passphrase = env_string("PM_API_PASSPHRASE");
  config.wallet_address = env_string("PM_WALLET_ADDRESS");

  config.min_net_edge = env_double("PM_MIN_NET_EDGE", config.min_net_edge);
  config.max_trade_usd = env_double("PM_MAX_TRADE_USD", config.max_trade_usd);
  config.market_limit = env_uint("PM_MARKET_LIMIT", config.market_limit);
  config.poll_interval_ms = env_uint("PM_POLL_INTERVAL_MS", config.poll_interval_ms);
  config.min_liquidity = env_double("PM_MIN_LIQUIDITY", config.min_liquidity);
  config.use_websocket = !env_truthy(std::getenv("PM_DISABLE_WS"));
  config.market_slug = env_string("PM_MARKET_SLUG");
  config.watch_status = env_truthy(std::getenv("PM_WATCH_STATUS"));
  config.verbose = env_truthy(std::getenv("PM_VERBOSE"));
  config.benchmark_mode = env_truthy(std::getenv("PM_BENCHMARK"));
  config.benchmark_iterations = env_uint("PM_BENCHMARK_ITERATIONS", config.benchmark_iterations);

  config.series_slug = env_string("PM_SERIES_SLUG").empty() ? config.series_slug
                                                             : env_string("PM_SERIES_SLUG");
  config.event_slug = env_string("PM_EVENT_SLUG");
  if (config.event_slug.empty()) {
    config.event_slug = env_string("PM_LOL_EVENT");
  }
  config.lol_discover = env_truthy(std::getenv("PM_LOL_DISCOVER"));
  config.lol_live_only = env_truthy(std::getenv("PM_LOL_LIVE_ONLY"));
  config.scan_moneyline = !env_truthy(std::getenv("PM_SKIP_MONEYLINE"));
  config.scan_game_winners = !env_truthy(std::getenv("PM_SKIP_GAME_WINNERS"));
  config.event_limit = env_uint("PM_EVENT_LIMIT", config.event_limit);
  config.focus_current_game = !env_truthy(std::getenv("PM_SCAN_ALL_GAMES"));
  config.game_loser_bid_max = env_double("PM_GAME_LOSER_BID_MAX", config.game_loser_bid_max);
  config.game_winner_ask_min = env_double("PM_GAME_WINNER_ASK_MIN", config.game_winner_ask_min);

  if (config.live_trading) {
    if (config.private_key.empty() || config.api_key.empty() ||
        config.api_secret.empty() || config.api_passphrase.empty() ||
        config.wallet_address.empty()) {
      throw std::runtime_error(
          "PM_LIVE_TRADING requires PM_PRIVATE_KEY, PM_API_KEY, PM_API_SECRET, "
          "PM_API_PASSPHRASE, and PM_WALLET_ADDRESS");
    }
  }

  return config;
}

}  // namespace pm
