#include "pm/benchmark.hpp"

#include "pm/auth/l2_auth.hpp"
#include "pm/fee_model.hpp"
#include "pm/order_book.hpp"

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
  nlohmann::json body = {
      {"order",
       {{"salt", "12345"},
        {"maker", "0x0000000000000000000000000000000000000001"},
        {"signer", "0x0000000000000000000000000000000000000001"},
        {"tokenId", market.yes_token_id},
        {"makerAmount", "1000000"},
        {"takerAmount", "2000000"},
        {"side", "BUY"},
        {"signatureType", 0},
        {"timestamp", "1713398400000"},
        {"metadata", "0x0000000000000000000000000000000000000000000000000000000000000000"},
        {"builder", "0x0000000000000000000000000000000000000000000000000000000000000000"},
        {"signature", "0x"}}},
      {"owner", "dry-run-benchmark"},
      {"orderType", "FOK"},
      {"postOnly", false},
  };
  (void)opp;
  return body.dump();
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
  auth_ms.reserve(config.benchmark_iterations);
  post_ms.reserve(config.benchmark_iterations);
  total_ms.reserve(config.benchmark_iterations);

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
    build_ms.push_back(time_call_ms([&] {
      ArbOpportunity stub;
      stub.market = market;
      stub.yes_price = 0.45;
      stub.no_price = 0.55;
      stub.max_size = 10.0;
      if (arb) {
        order_body = build_stub_order_body(market, *arb);
      } else {
        order_body = build_stub_order_body(market, stub);
      }
    }));

    std::map<std::string, std::string> headers;
    if (has_api_creds) {
      auth_ms.push_back(time_call_ms([&] {
        headers = auth::build_l2_headers(
            config.wallet_address,
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
  print_stats_row("order_build", result.order_build);
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
      parallel_books + result.arb_detect.avg_ms + result.order_build.avg_ms +
      (result.l2_auth.samples > 0 ? result.l2_auth.avg_ms : 0.0) + result.post_order_dry.avg_ms;

  std::cout << "\n  estimated hot-path (parallel books): " << std::fixed << std::setprecision(2)
            << estimated_trade << " ms\n"
            << "  sequential books would add: " << (seq_books - parallel_books) << " ms vs parallel\n"
            << "  " << result.post_order_note << "\n\n";
}

}  // namespace pm
