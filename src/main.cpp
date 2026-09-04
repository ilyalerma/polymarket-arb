#include "pm/arb_engine.hpp"
#include "pm/benchmark.hpp"
#include "pm/clob_client.hpp"
#include "pm/config.hpp"
#include "pm/fee_model.hpp"
#include "pm/gamma_client.hpp"
#include "pm/ws/market_ws.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running = false; }

std::string arb_kind_name(pm::ArbKind kind) {
  return kind == pm::ArbKind::BuyBoth ? "BUY_BOTH" : "SELL_BOTH";
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
            << "\n*** ARB @" << now_string() << " [" << arb_kind_name(opp.kind) << "] ***\n"
            << opp.market.question << '\n'
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
    std::cout << "[" << now_string() << "] waiting for book data...\n";
    return;
  }

  const double buy_both_cost = *yes_ask + *no_ask;
  const double sell_both_proceeds = *yes_bid + *no_bid;
  const double buy_edge = 1.0 - buy_both_cost;
  const double sell_edge = sell_both_proceeds - 1.0;

  std::cout << std::fixed << std::setprecision(3)
            << "[" << now_string() << "] " << market.question << '\n'
            << "  " << market.yes_outcome << "  bid=" << *yes_bid << " ask=" << *yes_ask
            << "  |  " << market.no_outcome << "  bid=" << *no_bid << " ask=" << *no_ask
            << '\n'
            << "  buy-both cost=" << buy_both_cost << " (edge " << buy_edge << ")"
            << "  |  sell-both proceeds=" << sell_both_proceeds << " (edge " << sell_edge
            << ")\n";
}

std::vector<pm::BinaryMarket> load_markets(
    const pm::Config& config,
    pm::GammaClient& gamma) {
  if (!config.market_slug.empty()) {
    auto market = gamma.fetch_market_by_slug(
        config.market_slug, config.benchmark_mode);
    if (!market && config.benchmark_mode) {
      constexpr const char* kEventPrefix = "lol-kt-dk-2026-09-04";
      if (config.market_slug.rfind(kEventPrefix, 0) == 0) {
        market = gamma.fetch_market_from_event(kEventPrefix, config.market_slug);
      }
    }
    if (!market) {
      throw std::runtime_error("market not found for slug: " + config.market_slug);
    }
    return {*market};
  }
  return gamma.fetch_active_markets(config.market_limit);
}

void scan_markets(
    const pm::Config& config,
    const std::vector<pm::BinaryMarket>& markets,
    pm::ClobClient& clob) {
  for (const auto& market : markets) {
    const auto yes_book = clob.fetch_book(market.yes_token_id);
    const auto no_book = clob.fetch_book(market.no_token_id);
    if (!yes_book || !no_book) {
      std::cerr << "Failed to fetch books for " << market.slug << '\n';
      continue;
    }

    pm::MarketBooks books;
    books.yes.update(*yes_book);
    books.no.update(*no_book);

    if (config.watch_status) {
      print_market_status(market, books);
    }

    if (auto buy = pm::detect_buy_both_arb(
            market, books.yes, books.no, config.min_net_edge, config.max_trade_usd)) {
      print_opportunity(*buy);
    }
    if (auto sell = pm::detect_sell_both_arb(
            market, books.yes, books.no, config.min_net_edge, config.max_trade_usd)) {
      print_opportunity(*sell);
    }
  }
}

void print_startup_banner(
    const pm::Config& config,
    const std::vector<pm::BinaryMarket>& markets) {
  if (!config.verbose) {
    return;
  }

  std::cout << "Polymarket arb watcher (scan-only, no trading)\n"
            << "  markets: " << markets.size() << '\n'
            << "  min_net_edge: " << config.min_net_edge << '\n'
            << "  fee category: " << (markets.empty() ? "n/a" : markets.front().category)
            << " (rate=" << (markets.empty() ? 0.0 : markets.front().taker_fee_rate) << ")\n";

  if (!markets.empty()) {
    std::cout << "  watching: " << markets.front().question << " [" << markets.front().slug
              << "]\n";
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
      } else if (!arg.empty() && arg.front() != '-') {
        config.market_slug = arg;
      }
    }
    config.live_trading = false;
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

  print_startup_banner(config, markets);

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

  if (config.use_websocket && markets.size() == 1) {
    pm::MarketWs ws(
        config,
        markets,
        [](const pm::ArbOpportunity& opp) { print_opportunity(opp); });
    ws.start();
    while (g_running) {
      scan_markets(config, markets, clob);
      std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
    }
    ws.stop();
    return 0;
  }

  while (g_running) {
    scan_markets(config, markets, clob);
    std::this_thread::sleep_for(std::chrono::milliseconds(config.poll_interval_ms));
  }

  return 0;
}
