#pragma once

#include "pm/types.hpp"

#include <optional>
#include <vector>

namespace pm {

class OrderBook {
 public:
  void update(const TokenBook& book);

  std::optional<double> best_bid() const;
  std::optional<double> best_ask() const;
  std::optional<double> depth_at_ask(double max_price) const;
  std::optional<double> depth_at_bid(double min_price) const;

  double tick_size() const { return tick_size_; }
  double min_order_size() const { return min_order_size_; }

 private:
  std::vector<BookLevel> bids_;
  std::vector<BookLevel> asks_;
  double tick_size_{0.01};
  double min_order_size_{1.0};
};

struct MarketBooks {
  OrderBook yes;
  OrderBook no;
};

}  // namespace pm
