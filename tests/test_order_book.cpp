#include "pm/order_book.hpp"

#include <iostream>

namespace {

bool expect_true(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  int failures = 0;
  pm::OrderBook book;

  pm::TokenBook snapshot;
  snapshot.bids = {{0.48, 100.0}, {0.46, 50.0}};
  snapshot.asks = {{0.52, 80.0}, {0.54, 40.0}};
  book.update(snapshot);

  failures += !expect_true(book.best_bid() && *book.best_bid() == 0.48, "initial best bid");
  failures += !expect_true(book.best_ask() && *book.best_ask() == 0.52, "initial best ask");

  book.apply_level(pm::BookSide::Bid, 0.49, 25.0);
  failures += !expect_true(book.best_bid() && *book.best_bid() == 0.49, "new best bid after insert");

  book.apply_level(pm::BookSide::Bid, 0.49, 0.0);
  failures += !expect_true(book.best_bid() && *book.best_bid() == 0.48, "best bid after level removal");

  book.apply_level(pm::BookSide::Ask, 0.52, 120.0);
  failures += !expect_true(
      book.depth_at_ask(0.52) && *book.depth_at_ask(0.52) == 120.0,
      "ask depth update in place");

  book.apply_level(pm::BookSide::Ask, 0.51, 15.0);
  failures += !expect_true(book.best_ask() && *book.best_ask() == 0.51, "new best ask after insert");

  if (failures == 0) {
    std::cout << "order book tests passed\n";
    return 0;
  }

  std::cerr << failures << " order book test(s) failed\n";
  return 1;
}
