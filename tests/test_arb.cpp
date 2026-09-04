#include "pm/fee_model.hpp"
#include "pm/order_book.hpp"
#include "pm/types.hpp"

#include <cmath>
#include <iostream>

namespace {

int expect_true(bool value, const char* message) {
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  int failures = 0;

  failures += expect_true(
      std::abs(pm::taker_fee_usd(100.0, 0.5, 0.05) - 1.25) < 1e-4,
      "fee at 50c should be 1.25");

  pm::BinaryMarket market;
  market.question = "Test market";
  market.min_order_size = 5.0;
  market.taker_fee_rate = 0.0;

  pm::TokenBook yes_book;
  yes_book.asks = {{0.45, 100.0}};
  yes_book.bids = {{0.44, 100.0}};
  yes_book.min_order_size = 5.0;

  pm::TokenBook no_book;
  no_book.asks = {{0.50, 100.0}};
  no_book.bids = {{0.49, 100.0}};
  no_book.min_order_size = 5.0;

  pm::OrderBook yes;
  pm::OrderBook no;
  yes.update(yes_book);
  no.update(no_book);

  const auto buy = pm::detect_buy_both_arb(market, yes, no, 0.001, 1000.0);
  failures += expect_true(buy.has_value(), "buy-both arb should exist");
  if (buy) {
    failures += expect_true(
        std::abs(buy->gross_edge - 0.05) < 1e-6, "gross edge should be 0.05");
  }

  const auto sell = pm::detect_sell_both_arb(market, yes, no, 0.001, 1000.0);
  failures += expect_true(!sell.has_value(), "sell-both arb should not exist");

  if (failures == 0) {
    std::cout << "All tests passed\n";
  }
  return failures;
}
