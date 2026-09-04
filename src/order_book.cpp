#include "pm/order_book.hpp"

#include <algorithm>
#include <cmath>

namespace pm {

namespace {

bool level_cmp_bid(const BookLevel& lhs, const BookLevel& rhs) {
  return lhs.price > rhs.price;
}

bool level_cmp_ask(const BookLevel& lhs, const BookLevel& rhs) {
  return lhs.price < rhs.price;
}

bool prices_equal(double lhs, double rhs) {
  return std::abs(lhs - rhs) < 1e-9;
}

}  // namespace

void OrderBook::update(const TokenBook& book) {
  bids_ = book.bids;
  asks_ = book.asks;
  tick_size_ = book.tick_size;
  min_order_size_ = book.min_order_size;
  std::sort(bids_.begin(), bids_.end(), level_cmp_bid);
  std::sort(asks_.begin(), asks_.end(), level_cmp_ask);
}

void OrderBook::apply_level(BookSide side, double price, double size) {
  auto& levels = side == BookSide::Bid ? bids_ : asks_;
  const auto existing = std::find_if(
      levels.begin(),
      levels.end(),
      [&](const BookLevel& level) { return prices_equal(level.price, price); });

  if (size <= 0.0) {
    if (existing != levels.end()) {
      levels.erase(existing);
    }
    return;
  }

  if (existing != levels.end()) {
    existing->size = size;
    return;
  }

  levels.push_back({price, size});
  if (side == BookSide::Bid) {
    std::sort(levels.begin(), levels.end(), level_cmp_bid);
  } else {
    std::sort(levels.begin(), levels.end(), level_cmp_ask);
  }
}

std::optional<double> OrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  return bids_.front().price;
}

std::optional<double> OrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  return asks_.front().price;
}

std::optional<double> OrderBook::depth_at_ask(double max_price) const {
  double total = 0.0;
  for (const auto& level : asks_) {
    if (level.price > max_price) {
      break;
    }
    total += level.size;
  }
  return total > 0.0 ? std::optional<double>{total} : std::nullopt;
}

std::optional<double> OrderBook::depth_at_bid(double min_price) const {
  double total = 0.0;
  for (const auto& level : bids_) {
    if (level.price < min_price) {
      break;
    }
    total += level.size;
  }
  return total > 0.0 ? std::optional<double>{total} : std::nullopt;
}

}  // namespace pm
