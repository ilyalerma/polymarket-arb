#include "pm/arb_engine.hpp"
#include "pm/auth/l2_auth.hpp"
#include "pm/benchmark.hpp"
#include "pm/clob_client.hpp"
#include "pm/config.hpp"
#include "pm/fee_model.hpp"
#include "pm/game_state.hpp"
#include "pm/gamma_client.hpp"
#include "pm/market_tracker.hpp"
#include "pm/order_chamber.hpp"
#include "pm/trade_fire.hpp"
#include "pm/ws/market_ws.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running = false; }

std::string arb_kind_name(pm::ArbKind kind) {
  return kind == pm::ArbKind::BuyBoth ? "BUY_BOTH" : "SELL_BOTH";
}

std::string market_kind_label(const pm::BinaryMarket& market) {
  if (market.market_kind == pm::MarketKind::Moneyline) {
    return "MONEYLINE";
  }
  if (market.market_kind == pm::MarketKind::GameWinner) {
    return market.group_title.empty() ? "GAME_WINNER" : market.group_title;
  }
  return "BINARY";
}

std::string now_string() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
  return buf;
}

void print_opportunity(const pm::ArbOpportunity& opp) {
  std::cout << std::fixed << std::setprecision(4)
            << "\n*** ARB @" << now_string() << " [" << arb_kind_name(opp.kind) << "] "
            << market_kind_label(opp.market) << " ***\n";
  if (!opp.market.event_title.empty()) {
    std::cout << opp.market.event_title << '\n';
  }
  std::cout << opp.market.question << " [" << opp.market.slug << "]\n"
            << "  " << opp.market.yes_outcome << " @ " << opp.yes_price
            << "  |  " << opp.market.no_outcome << " @ " << opp.no_price << '\n'
            << "  gross_edge=" << opp.gross_edge << " net_edge=" << opp.net_edge
            << " size=" << opp.max_size << " est_profit=$" << opp.expected_profit_usd
            << " fees=$" << opp.fee_estimate << '\n';
}

void print_market_status(
    const pm::BinaryMarket& market,
    const pm::MarketBooks& books) {
  const auto yes_bid = books.yes.best_bid();
  const auto yes_ask = books.yes.best_ask();
  const auto no_bid = books.no.best_bid();
  const auto no_ask = books.no.best_ask();
  if (!yes_bid || !yes_ask || !no_bid || !no_ask) {
    std::cout << "[" << now_string() << "] waiting for book data: " << market.slug << '\n';
    return;
  }

  const double buy_both_cost = *yes_ask + *no_ask;
  const double sell_both_proceeds = *yes_bid + *no_bid;
  const double buy_edge = 1.0 - buy_both_cost;
  const double sell_edge = sell_both_proceeds - 1.0;

  std::cout << std::fixed << std::setprecision(3)
            << "[" << now_string() << "] [" << market_kind_label(market) << "] "
            << market.question << '\n'
            << "  " << market.yes_outcome << "  bid=" << *yes_bid << " ask=" << *yes_ask
            << "  |  " << market.no_outcome << "  bid=" << *no_bid << " ask=" << *no_ask
            << '\n'
            << "  buy-both cost=" << buy_both_cost << " (edge " << buy_edge << ")"
            << "  |  sell-both proceeds=" << sell_both_proceeds << " (edge " << sell_edge
            << ")\n";
}

bool looks_like_game_market_slug(const std::string& slug) {
  static const std::regex pattern(R"(-game\d+$)");
  return std::regex_search(slug, pattern);
}

std::string infer_event_slug_from_market(const std::string& market_slug) {
  static const std::regex game_suffix(R"(-game\d+$)");
  return std::regex_replace(market_slug, game_suffix, "");
}

std::vector<pm::BinaryMarket> load_lol_markets(
    const pm::Config& config,
    pm::GammaClient& gamma) {
  std::vector<pm::BinaryMarket> markets;

  if (!config.event_slug.empty()) {
    const auto event = gamma.fetch_lol_event(config.event_slug);
    if (!event) {
      throw std::runtime_error("LoL event not found: " + config.event_slug);
    }
    return gamma.extract_arb_markets(
        *event, config.scan_moneyline, config.scan_game_winners);
  }

  const auto events =
      gamma.fetch_lol_events(config.series_slug, config.event_limit, config.lol_live_only);
  for (const auto& event : events) {
    const auto event_markets = gamma.extract_arb_markets(
        event, config.scan_moneyline, config.scan_game_winners);
    markets.insert(markets.end(), event_markets.begin(), event_markets.end());
  }
  return markets;
}

std::vector<pm::BinaryMarket> load_markets(
    const pm::Config& config,
    pm::GammaClient& gamma) {
  if (!config.market_slug.empty()) {
    auto market = gamma.fetch_market_by_slug(
        config.market_slug, config.benchmark_mode);
    if (!market && config.benchmark_mode) {
      const auto event_slug = infer_event_slug_from_market(config.market_slug);
      market = gamma.fetch_market_from_event(event_slug, config.market_slug);
    }
    if (!market) {
      throw std::runtime_error("market not found for slug: " + config.market_slug);
    }
    return {*market};
  }

  if (config.lol_discover || !config.event_slug.empty()) {
    return load_lol_markets(config, gamma);
  }

  return gamma.fetch_active_markets(config.market_limit);
}

void scan_markets(
    const pm::Config& config,
    const std::vector<pm::BinaryMarket>& markets,
    pm::ClobClient& clob,
    pm::GameTracker& game_tracker,
    pm::OrderChamberRegistry& chambers) {
  std::unordered_map<std::string, pm::MarketBooks> books_by_condition;

  for (const auto& market : markets) {
    const auto yes_book = clob.fetch_book(market.yes_token_id);
    const auto no_book = clob.fetch_book(market.no_token_id);
    if (!yes_book || !no_book) {
      if (config.verbose) {
        std::cerr << "Failed to fetch books for " << market.slug << '\n';
      }
      continue;
    }

    pm::MarketBooks books;
    books.yes.update(*yes_book);
    books.no.update(*no_book);
    books_by_condition[market.condition_id] = books;
  }

  const auto tracker =
      pm::build_game_tracker(config, markets, books_by_condition, game_tracker);

  for (const auto& market : markets) {
    const auto books_it = books_by_condition.find(market.condition_id);
    if (books_it == books_by_condition.end()) {
      continue;
    }

    if (!pm::should_scan_for_arb(config, market, tracker)) {
      continue;
    }

    const auto& books = books_it->second;
    if (config.watch_status) {
      print_market_status(market, books);
    }

    if (auto buy = pm::detect_buy_both_arb(
            market, books.yes, books.no, config.min_net_edge, config.max_trade_usd)) {
      print_opportunity(*buy);
      try_fire_arb(config, clob, chambers, *buy);
    }
    if (auto sell = pm::detect_sell_both_arb(
            market, books.yes, books.no, config.min_net_edge, config.max_trade_usd)) {
      print_opportunity(*sell);
      try_fire_arb(config, clob, chambers, *sell);
    }
  }
}

void print_startup_banner(
    const pm::Config& config,
    const std::vector<pm::BinaryMarket>& markets) {
  if (!config.verbose) {
    return;
  }

  std::cout << "Polymarket LoL arb watcher (scan-only, no trading)\n"
            << "  markets: " << markets.size() << '\n'
            << "  book feed: " << (config.use_websocket ? "websocket" : "rest") << '\n'
            << "  min_net_edge: " << config.min_net_edge << '\n';

  if (config.lol_discover || !config.event_slug.empty()) {
    std::cout << "  series: " << config.series_slug << '\n'
              << "  moneyline: " << (config.scan_moneyline ? "yes" : "no")
              << "  game winners: " << (config.scan_game_winners ? "yes" : "no") << '\n';
    if (!config.event_slug.empty()) {
      std::cout << "  event: " << config.event_slug << '\n';
    }
    if (config.lol_live_only) {
      std::cout << "  live events only: yes\n";
    }
    if (config.focus_current_game) {
      std::cout << "  sequential game scan: yes (Game 1 -> Game 2 -> ... -> moneyline decider)\n"
                << "  game finished: loser ask <= " << config.game_loser_ask_max
                << ", winner bid >= " << config.game_winner_bid_min << '\n';
    }
  }

  std::unordered_map<std::string, std::uint32_t> per_event;
  for (const auto& market : markets) {
    const std::string key = market.event_slug.empty() ? market.slug : market.event_slug;
    ++per_event[key];
  }
  for (const auto& [event_slug, count] : per_event) {
    std::cout << "  - " << event_slug << ": " << count << " market(s)\n";
  }

  std::cout << "  press Ctrl+C to stop\n\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  pm::Config config;
  try {
    config = pm::load_config_from_env();
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--benchmark") {
        config.benchmark_mode = true;
      } else if (arg == "--benchmark-book") {
        config.book_benchmark_mode = true;
      } else if (arg == "--lol") {
        config.lol_discover = true;
      } else if (arg == "--event" && i + 1 < argc) {
        config.event_slug = argv[++i];
        config.lol_discover = false;
      } else if (!arg.empty() && arg.front() != '-') {
        if (looks_like_game_market_slug(arg) || arg.find("-game") != std::string::npos) {
          config.market_slug = arg;
        } else if (arg.rfind("lol-", 0) == 0) {
          config.event_slug = arg;
        } else {
          config.market_slug = arg;
        }
      }
    }

    if (config.market_slug.empty() && config.event_slug.empty() && !config.benchmark_mode &&
        !config.book_benchmark_mode) {
      config.lol_discover = true;
    }
  } catch (const std::exception& ex) {
    std::cerr << "Config error: " << ex.what() << '\n';
    return 1;
  }

  pm::HttpClient gamma_http(config.gamma_url);
  pm::HttpClient clob_http(config.clob_url);
  pm::GammaClient gamma(std::move(gamma_http));
  pm::ClobClient clob(std::move(clob_http));

  std::vector<pm::BinaryMarket> markets;
  try {
    markets = load_markets(config, gamma);
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  if (markets.empty() && !config.benchmark_mode && !config.book_benchmark_mode) {
    std::cerr << "No tradeable LoL moneyline or game-winner markets found.\n"
              << "Try PM_LOL_LIVE_ONLY=0 or set PM_EVENT_SLUG to a specific match.\n";
    return 1;
  }

  print_startup_banner(config, markets);
  if (!config.verbose) {
    std::cout << "Watching " << markets.size() << " market(s) — silent until arb. "
              << "Use PM_VERBOSE=1 or PM_WATCH_STATUS=1 for live output.\n";
  }

  pm::OrderChamberRegistry chambers;
  chambers.prime_all(markets, config);
  if (config.verbose) {
    std::cout << "  order chambers primed: " << markets.size() << " market(s)\n";
  }

  if (config.book_benchmark_mode) {
    if (markets.empty()) {
      std::cerr << "No market loaded for book benchmark\n";
      return 1;
    }
    const auto result =
        pm::run_book_update_benchmark(config, markets.front(), clob);
    pm::print_book_update_benchmark_result(result, markets.front());
    return 0;
  }

  if (config.benchmark_mode) {
    if (markets.empty()) {
      std::cerr << "No market loaded for benchmark\n";
      return 1;
    }
    const auto result =
        pm::run_dry_trade_benchmark(config, markets.front(), gamma, clob);
    pm::print_benchmark_result(result, markets.front());
    return 0;
  }

  if (config.use_websocket) {
    pm::MarketWs::StatusCallback on_status;
    if (config.watch_status) {
      on_status = [](const pm::BinaryMarket& market, const pm::MarketBooks& books) {
        print_market_status(market, books);
      };
    }

    pm::MarketWs ws(
        config,
        markets,
        &clob,
        &chambers,
        [](const pm::ArbOpportunity& opp) { print_opportunity(opp); },
        std::move(on_status));
    ws.start();
    while (g_running) {
      if (config.lol_discover || !config.event_slug.empty()) {
        try {
          markets = load_markets(config, gamma);
          chambers.prime_all(markets, config);
          ws.set_markets(markets);
        } catch (const std::exception& ex) {
          std::cerr << "Market refresh failed: " << ex.what() << '\n';
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
    }
    ws.stop();
    return 0;
  }

  pm::GameTracker game_tracker;
  while (g_running) {
    if (config.lol_discover || !config.event_slug.empty()) {
      try {
        markets = load_markets(config, gamma);
        chambers.prime_all(markets, config);
      } catch (const std::exception& ex) {
        std::cerr << "Market refresh failed: " << ex.what() << '\n';
      }
    }
    scan_markets(config, markets, clob, game_tracker, chambers);
    std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
  }

  return 0;
}
