#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pm {

struct BookLevel {
  double price{0.0};
  double size{0.0};
};

struct TokenBook {
  std::string token_id;
  std::vector<BookLevel> bids;
  std::vector<BookLevel> asks;
  double tick_size{0.01};
  double min_order_size{1.0};
};

struct BinaryMarket {
  std::string condition_id;
  std::string question;
  std::string slug;
  std::string yes_token_id;
  std::string no_token_id;
  std::string yes_outcome;
  std::string no_outcome;
  std::string category;
  bool neg_risk{false};
  bool accepting_orders{false};
  double tick_size{0.01};
  double min_order_size{5.0};
  double taker_fee_rate{0.05};
  double liquidity{0.0};
  double volume_24h{0.0};
};

enum class ArbKind { BuyBoth, SellBoth };

struct ArbOpportunity {
  BinaryMarket market;
  ArbKind kind{ArbKind::BuyBoth};
  double yes_price{0.0};
  double no_price{0.0};
  double gross_edge{0.0};
  double fee_estimate{0.0};
  double net_edge{0.0};
  double max_size{0.0};
  double expected_profit_usd{0.0};
};

}  // namespace pm
