#include "pm/benchmark.hpp"

#include "pm/auth/l2_auth.hpp"
#include "pm/fee_model.hpp"
#include "pm/order_book.hpp"
#include "pm/order_chamber.hpp"
#include "pm/ws/book_updates.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>

namespace pm {

namespace {

using Clock = std::chrono::steady_clock;

double to_ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

LatencyStats compute_stats(std::vector<double> samples) {
  LatencyStats stats;
  if (samples.empty()) {
    return stats;
  }
  std::sort(samples.begin(), samples.end());
  stats.samples = static_cast<std::uint32_t>(samples.size());
  stats.min_ms = samples.front();
  stats.max_ms = samples.back();
  stats.avg_ms = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
  stats.p50_ms = samples[samples.size() / 2];
  const std::size_t p95_idx =
      std::min(samples.size() - 1, static_cast<std::size_t>(samples.size() * 0.95));
  stats.p95_ms = samples[p95_idx];
  return stats;
}

template <typename Fn>
double time_call_ms(Fn&& fn) {
  const auto start = Clock::now();
  fn();
  return to_ms(Clock::now() - start);
}

std::string build_stub_order_body(const BinaryMarket& market, const ArbOpportunity& opp) {
  OrderChamber chamber;
  chamber.prime(market, Config{}, opp.kind);
  chamber.aim(opp);
  FiredShot shot;
  if (!chamber.fire_into(shot, opp.max_size, next_order_salt())) {
    return {};
  }
  return std::string(shot.yes.view());
}

void print_stats_row(const char* label, const LatencyStats& stats) {
  if (stats.samples == 0) {
    std::cout << "  " << std::left << std::setw(22) << label << "  (skipped)\n";
    return;
  }
  std::cout << std::fixed << std::setprecision(2)
            << "  " << std::left << std::setw(22) << label
            << "  avg=" << std::setw(8) << stats.avg_ms
            << "  p50=" << std::setw(8) << stats.p50_ms
            << "  p95=" << std::setw(8) << stats.p95_ms
            << "  min=" << std::setw(8) << stats.min_ms
            << "  max=" << stats.max_ms << '\n';
}

void print_stats_row_us(const char* label, const LatencyStats& stats) {
  if (stats.samples == 0) {
    std::cout << "  " << std::left << std::setw(28) << label << "  (skipped)\n";
    return;
  }
  std::cout << std::fixed << std::setprecision(1)
            << "  " << std::left << std::setw(28) << label
            << "  avg=" << std::setw(10) << stats.avg_ms << " us"
            << "  p50=" << std::setw(10) << stats.p50_ms << " us"
            << "  p95=" << std::setw(10) << stats.p95_ms << " us"
            << "  min=" << std::setw(10) << stats.min_ms << " us"
            << "  max=" << stats.max_ms << " us\n";
}

LatencyStats compute_stats_us(std::vector<double> samples_us) {
  return compute_stats(std::move(samples_us));
}

}  // namespace

DryRunBenchmarkResult run_dry_trade_benchmark(
    const Config& config,
    const BinaryMarket& market,
    GammaClient& gamma,
    ClobClient& clob) {
  DryRunBenchmarkResult result;
  result.iterations = config.benchmark_iterations;

  std::vector<double> gamma_ms;
  std::vector<double> yes_ms;
  std::vector<double> no_ms;
  std::vector<double> parallel_ms;
  std::vector<double> arb_ms;
  std::vector<double> build_ms;
  std::vector<double> chamber_aim_ms;
  std::vector<double> chamber_fire_ms;
  std::vector<double> auth_ms;
  std::vector<double> post_ms;
  std::vector<double> total_ms;

  const bool has_api_creds = !config.api_key.empty() && !config.api_secret.empty() &&
                             !config.api_passphrase.empty() && !config.wallet_address.empty();

  gamma_ms.reserve(config.benchmark_iterations);
  yes_ms.reserve(config.benchmark_iterations);
  no_ms.reserve(config.benchmark_iterations);
  parallel_ms.reserve(config.benchmark_iterations);
  arb_ms.reserve(config.benchmark_iterations);
  build_ms.reserve(config.benchmark_iterations);
  chamber_aim_ms.reserve(config.benchmark_iterations);
  chamber_fire_ms.reserve(config.benchmark_iterations);
  auth_ms.reserve(config.benchmark_iterations);
  post_ms.reserve(config.benchmark_iterations);
  total_ms.reserve(config.benchmark_iterations);

  OrderChamberRegistry registry;
  registry.prime_all({market}, config, nullptr);
  OrderChamber* chamber = registry.chamber_for(market.slug, ArbKind::BuyBoth);

  for (std::uint32_t i = 0; i < config.benchmark_iterations; ++i) {
  const auto total_start = Clock::now();

    gamma_ms.push_back(time_call_ms([&] {
      (void)gamma.fetch_market_by_slug(market.slug, true);
    }));

    double yes_book_ms = 0.0;
    double no_book_ms = 0.0;
    std::optional<TokenBook> yes_book;
    std::optional<TokenBook> no_book;

    yes_book_ms = time_call_ms([&] { yes_book = clob.fetch_book(market.yes_token_id); });
    no_book_ms = time_call_ms([&] { no_book = clob.fetch_book(market.no_token_id); });
    yes_ms.push_back(yes_book_ms);
    no_ms.push_back(no_book_ms);

    double parallel_book_ms = 0.0;
    parallel_book_ms = time_call_ms([&] {
      auto yes_future = std::async(std::launch::async, [&] {
        return clob.fetch_book(market.yes_token_id);
      });
      auto no_future = std::async(std::launch::async, [&] {
        return clob.fetch_book(market.no_token_id);
      });
      (void)yes_future.get();
      (void)no_future.get();
    });
    parallel_ms.push_back(parallel_book_ms);

    std::optional<ArbOpportunity> arb;
    arb_ms.push_back(time_call_ms([&] {
      if (!yes_book || !no_book) {
        return;
      }
      MarketBooks books;
      books.yes.update(*yes_book);
      books.no.update(*no_book);
      arb = detect_buy_both_arb(
          market, books.yes, books.no, config.min_net_edge, config.max_trade_usd);
      if (!arb) {
        arb = detect_sell_both_arb(
            market, books.yes, books.no, config.min_net_edge, config.max_trade_usd);
      }
    }));

    std::string order_body;
    ArbOpportunity shot_opp;
    shot_opp.market = market;
    shot_opp.kind = ArbKind::BuyBoth;
    shot_opp.yes_price = 0.45;
    shot_opp.no_price = 0.55;
    shot_opp.max_size = 10.0;
    if (arb) {
      shot_opp = *arb;
      chamber = registry.chamber_for(market.slug, arb->kind);
    }
    if (!chamber) {
      chamber = registry.chamber_for(market.slug, ArbKind::BuyBoth);
    }

    chamber_aim_ms.push_back(time_call_ms([&] {
      if (chamber) {
        chamber->aim(shot_opp);
      }
    }));

    build_ms.push_back(time_call_ms([&] {
      order_body = build_stub_order_body(market, shot_opp);
    }));

    chamber_fire_ms.push_back(time_call_ms([&] {
      if (!chamber) {
        return;
      }
      FiredShot shot;
      if (chamber->fire_into(shot, shot_opp.max_size, next_order_salt())) {
        order_body.assign(shot.yes.data, shot.yes.size);
      }
    }));

    std::map<std::string, std::string> headers;
    if (has_api_creds) {
      auth_ms.push_back(time_call_ms([&] {
        headers = auth::build_l2_headers(
            config.signer_address.empty() ? config.wallet_address : config.signer_address,
            config.api_key,
            config.api_secret,
            config.api_passphrase,
            "POST",
            "/order",
            order_body);
      }));
    }

    post_ms.push_back(time_call_ms([&] {
      if (has_api_creds) {
        (void)clob.post_order(order_body, headers);
      } else {
        (void)clob.post_order(
            order_body,
            {{"POLY_ADDRESS", "0x0000000000000000000000000000000000000001"},
             {"POLY_SIGNATURE", "dry-run"},
             {"POLY_TIMESTAMP", "0"},
             {"POLY_API_KEY", "dry-run"},
             {"POLY_PASSPHRASE", "dry-run"}});
      }
    }));

    total_ms.push_back(to_ms(Clock::now() - total_start));
  }

  result.gamma_lookup = compute_stats(std::move(gamma_ms));
  result.book_yes = compute_stats(std::move(yes_ms));
  result.book_no = compute_stats(std::move(no_ms));
  result.books_parallel = compute_stats(std::move(parallel_ms));
  result.arb_detect = compute_stats(std::move(arb_ms));
  result.order_build = compute_stats(std::move(build_ms));
  result.chamber_aim = compute_stats(std::move(chamber_aim_ms));
  result.chamber_fire = compute_stats(std::move(chamber_fire_ms));
  result.l2_auth = compute_stats(std::move(auth_ms));
  result.post_order_dry = compute_stats(std::move(post_ms));
  result.total_sequential = compute_stats(std::move(total_ms));

  if (has_api_creds) {
    result.post_order_note =
        "POST /order sent with real L2 auth + stub order; rejected by API (no trade placed)";
  } else {
    result.post_order_note =
        "POST /order sent with dummy auth + stub order; rejected by API (no trade placed)";
  }

  return result;
}

void print_benchmark_result(const DryRunBenchmarkResult& result, const BinaryMarket& market) {
  std::cout << "\nDry-run trade benchmark (" << result.iterations << " iterations)\n"
            << "  market: " << market.question << " [" << market.slug << "]\n"
            << "  note: measures latency only — orders are invalid and never execute\n\n";

  print_stats_row("gamma_lookup", result.gamma_lookup);
  print_stats_row("book_yes (seq)", result.book_yes);
  print_stats_row("book_no (seq)", result.book_no);
  print_stats_row("books (parallel)", result.books_parallel);
  print_stats_row("arb_detect", result.arb_detect);
  print_stats_row("chamber_aim", result.chamber_aim);
  print_stats_row("chamber_fire", result.chamber_fire);
  print_stats_row("order_build (legacy)", result.order_build);
  if (result.l2_auth.samples > 0) {
    print_stats_row("l2_auth", result.l2_auth);
  } else {
    std::cout << "  l2_auth                (skipped — set PM_API_KEY etc. to benchmark auth)\n";
  }
  print_stats_row("post_order_dry", result.post_order_dry);
  print_stats_row("total_pipeline", result.total_sequential);

  const double seq_books =
      result.book_yes.avg_ms + result.book_no.avg_ms;
  const double parallel_books = result.books_parallel.avg_ms;
  const double estimated_trade =
      parallel_books + result.arb_detect.avg_ms + result.chamber_aim.avg_ms +
      result.chamber_fire.avg_ms +
      (result.l2_auth.samples > 0 ? result.l2_auth.avg_ms : 0.0) + result.post_order_dry.avg_ms;

  std::cout << "\n  estimated hot-path (parallel books): " << std::fixed << std::setprecision(2)
            << estimated_trade << " ms\n"
            << "  sequential books would add: " << (seq_books - parallel_books) << " ms vs parallel\n"
            << "  " << result.post_order_note << "\n\n";
}

BookUpdateBenchmarkResult run_book_update_benchmark(
    const Config& config,
    const BinaryMarket& market,
    ClobClient& clob) {
  BookUpdateBenchmarkResult result;
  result.iterations = config.benchmark_iterations;

  std::vector<double> apply_level_us;
  std::vector<double> parse_apply_us;
  std::vector<double> incremental_us;
  std::vector<double> rest_refetch_ms;
  std::vector<double> rest_arb_ms;

  apply_level_us.reserve(config.benchmark_iterations);
  parse_apply_us.reserve(config.benchmark_iterations);
  incremental_us.reserve(config.benchmark_iterations);
  rest_refetch_ms.reserve(config.benchmark_iterations);
  rest_arb_ms.reserve(config.benchmark_iterations);

  OrderBook yes_book;
  OrderBook no_book;
  const auto yes_snapshot = clob.fetch_book(market.yes_token_id);
  const auto no_snapshot = clob.fetch_book(market.no_token_id);
  if (yes_snapshot && no_snapshot) {
    yes_book.update(*yes_snapshot);
    no_book.update(*no_snapshot);
  } else {
    TokenBook seed;
    seed.bids = {{0.44, 1000.0}, {0.43, 500.0}};
    seed.asks = {{0.46, 1000.0}, {0.47, 500.0}};
    yes_book.update(seed);
    no_book.update(seed);
  }

  nlohmann::json price_change_event = {
      {"event_type", "price_change"},
      {"market", market.condition_id},
      {"price_changes", nlohmann::json::array()},
  };
  price_change_event["price_changes"].push_back({
      {"asset_id", market.yes_token_id},
      {"price", "0.45"},
      {"size", "2500.0"},
      {"side", "BUY"},
      {"best_bid", "0.45"},
      {"best_ask", "0.46"},
  });
  price_change_event["price_changes"].push_back({
      {"asset_id", market.no_token_id},
      {"price", "0.55"},
      {"size", "1800.0"},
      {"side", "SELL"},
      {"best_bid", "0.54"},
      {"best_ask", "0.55"},
  });
  const std::string price_change_message = price_change_event.dump();

  for (std::uint32_t i = 0; i < config.benchmark_iterations; ++i) {
    const double bid_price = 0.40 + static_cast<double>(i % 50) * 0.001;
    apply_level_us.push_back(time_call_ms([&] {
      yes_book.apply_level(BookSide::Bid, bid_price, 100.0 + static_cast<double>(i));
    }) * 1000.0);

    parse_apply_us.push_back(time_call_ms([&] {
      const auto change = nlohmann::json::parse(price_change_message).at("price_changes").at(0);
      ws::apply_price_change(yes_book, change);
    }) * 1000.0);

    incremental_us.push_back(time_call_ms([&] {
      const auto event = nlohmann::json::parse(price_change_message);
      OrderBook local_yes = yes_book;
      OrderBook local_no = no_book;
      for (const auto& change : event.at("price_changes")) {
        const auto token_id = change.value("asset_id", "");
        if (token_id == market.yes_token_id) {
          ws::apply_price_change(local_yes, change);
        } else if (token_id == market.no_token_id) {
          ws::apply_price_change(local_no, change);
        }
      }
      (void)detect_buy_both_arb(
          market, local_yes, local_no, config.min_net_edge, config.max_trade_usd);
      (void)detect_sell_both_arb(
          market, local_yes, local_no, config.min_net_edge, config.max_trade_usd);
    }) * 1000.0);

    rest_refetch_ms.push_back(time_call_ms([&] {
      (void)clob.fetch_book(market.yes_token_id);
      (void)clob.fetch_book(market.no_token_id);
    }));

    rest_arb_ms.push_back(time_call_ms([&] {
      const auto yes = clob.fetch_book(market.yes_token_id);
      const auto no = clob.fetch_book(market.no_token_id);
      if (!yes || !no) {
        return;
      }
      MarketBooks books;
      books.yes.update(*yes);
      books.no.update(*no);
      (void)detect_buy_both_arb(
          market, books.yes, books.no, config.min_net_edge, config.max_trade_usd);
      (void)detect_sell_both_arb(
          market, books.yes, books.no, config.min_net_edge, config.max_trade_usd);
    }));
  }

  result.apply_level_us = compute_stats_us(std::move(apply_level_us));
  result.parse_and_apply_us = compute_stats_us(std::move(parse_apply_us));
  result.incremental_pipeline_us = compute_stats_us(std::move(incremental_us));
  result.rest_refetch_ms = compute_stats(std::move(rest_refetch_ms));
  result.rest_plus_arb_ms = compute_stats(std::move(rest_arb_ms));
  return result;
}

void print_book_update_benchmark_result(
    const BookUpdateBenchmarkResult& result,
    const BinaryMarket& market) {
  std::cout << "\nBook update benchmark (" << result.iterations << " iterations)\n"
            << "  market: " << market.question << " [" << market.slug << "]\n"
            << "  compares incremental WS path vs legacy REST refetch\n\n";

  print_stats_row_us("apply_level (hot)", result.apply_level_us);
  print_stats_row_us("parse + apply (1 leg)", result.parse_and_apply_us);
  print_stats_row_us("incremental + arb", result.incremental_pipeline_us);
  print_stats_row("rest_refetch (2 books)", result.rest_refetch_ms);
  print_stats_row("rest_refetch + arb", result.rest_plus_arb_ms);

  const double speedup =
      result.rest_plus_arb_ms.avg_ms > 0.0
          ? (result.rest_plus_arb_ms.avg_ms * 1000.0) / result.incremental_pipeline_us.avg_ms
          : 0.0;

  std::cout << "\n  incremental vs REST+arb speedup: " << std::fixed << std::setprecision(0)
            << speedup << "x faster (local CPU only; excludes network WS delivery)\n"
            << "  note: REST rows include real HTTP to Polymarket\n\n";
}

}  // namespace pm
