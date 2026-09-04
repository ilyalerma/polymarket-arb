#pragma once

#include "pm/clob_client.hpp"
#include "pm/config.hpp"
#include "pm/gamma_client.hpp"
#include "pm/types.hpp"

#include <cstdint>
#include <vector>

namespace pm {

struct LatencyStats {
  double min_ms{0.0};
  double max_ms{0.0};
  double avg_ms{0.0};
  double p50_ms{0.0};
  double p95_ms{0.0};
  std::uint32_t samples{0};
};

struct DryRunBenchmarkResult {
  LatencyStats gamma_lookup;
  LatencyStats book_yes;
  LatencyStats book_no;
  LatencyStats books_parallel;
  LatencyStats arb_detect;
  LatencyStats order_build;
  LatencyStats l2_auth;
  LatencyStats post_order_dry;
  LatencyStats total_sequential;
  std::uint32_t iterations{0};
  bool post_order_rejected{true};
  std::string post_order_note;
};

DryRunBenchmarkResult run_dry_trade_benchmark(
    const Config& config,
    const BinaryMarket& market,
    GammaClient& gamma,
    ClobClient& clob);

void print_benchmark_result(const DryRunBenchmarkResult& result, const BinaryMarket& market);

}  // namespace pm
