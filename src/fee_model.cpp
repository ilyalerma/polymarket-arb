#include "pm/fee_model.hpp"

#include <algorithm>
#include <cmath>

namespace pm {

namespace {

constexpr double kFeePrecision = 0.00001;

double round_fee(double fee) {
  return std::round(fee / kFeePrecision) * kFeePrecision;
}

double clamp_size(
    double yes_depth,
    double no_depth,
    double min_order_size,
    double max_trade_usd,
    double yes_price,
    double no_price) {
  const double max_by_usd = max_trade_usd / std::max(yes_price + no_price, 1e-9);
  double size = std::min({yes_depth, no_depth, max_by_usd});
  if (size < min_order_size) {
    return 0.0;
  }
  return size;
}

}  // namespace

double taker_fee_usd(double shares, double price, double fee_rate) {
  if (fee_rate <= 0.0 || shares <= 0.0) {
    return 0.0;
  }
  const double raw = shares * fee_rate * price * (1.0 - price);
  const double rounded = round_fee(raw);
  return rounded < kFeePrecision ? 0.0 : rounded;
}

double total_taker_fee_usd(
    double shares,
    double yes_price,
    double no_price,
    double fee_rate) {
  return taker_fee_usd(shares, yes_price, fee_rate) +
         taker_fee_usd(shares, no_price, fee_rate);
}

std::optional<ArbOpportunity> detect_buy_both_arb(
    const BinaryMarket& market,
    const OrderBook& yes_book,
    const OrderBook& no_book,
    double min_net_edge,
    double max_trade_usd) {
  const auto yes_ask = yes_book.best_ask();
  const auto no_ask = no_book.best_ask();
  if (!yes_ask || !no_ask) {
    return std::nullopt;
  }

  const double gross_edge = 1.0 - (*yes_ask + *no_ask);
  if (gross_edge <= 0.0) {
    return std::nullopt;
  }

  const auto yes_depth = yes_book.depth_at_ask(*yes_ask);
  const auto no_depth = no_book.depth_at_ask(*no_ask);
  if (!yes_depth || !no_depth) {
    return std::nullopt;
  }

  const double min_size = std::max(
      {market.min_order_size, yes_book.min_order_size(), no_book.min_order_size()});
  const double size = clamp_size(
      *yes_depth, *no_depth, min_size, max_trade_usd, *yes_ask, *no_ask);
  if (size <= 0.0) {
    return std::nullopt;
  }

  const double fee_estimate =
      total_taker_fee_usd(size, *yes_ask, *no_ask, market.taker_fee_rate);
  const double net_edge = gross_edge - (fee_estimate / size);
  if (net_edge < min_net_edge) {
    return std::nullopt;
  }

  ArbOpportunity opp;
  opp.market = market;
  opp.kind = ArbKind::BuyBoth;
  opp.yes_price = *yes_ask;
  opp.no_price = *no_ask;
  opp.gross_edge = gross_edge;
  opp.fee_estimate = fee_estimate;
  opp.net_edge = net_edge;
  opp.max_size = size;
  opp.expected_profit_usd = net_edge * size;
  return opp;
}

std::optional<ArbOpportunity> detect_sell_both_arb(
    const BinaryMarket& market,
    const OrderBook& yes_book,
    const OrderBook& no_book,
    double min_net_edge,
    double max_trade_usd) {
  const auto yes_bid = yes_book.best_bid();
  const auto no_bid = no_book.best_bid();
  if (!yes_bid || !no_bid) {
    return std::nullopt;
  }

  const double gross_edge = (*yes_bid + *no_bid) - 1.0;
  if (gross_edge <= 0.0) {
    return std::nullopt;
  }

  const auto yes_depth = yes_book.depth_at_bid(*yes_bid);
  const auto no_depth = no_book.depth_at_bid(*no_bid);
  if (!yes_depth || !no_depth) {
    return std::nullopt;
  }

  const double min_size = std::max(
      {market.min_order_size, yes_book.min_order_size(), no_book.min_order_size()});
  const double size = clamp_size(
      *yes_depth, *no_depth, min_size, max_trade_usd, *yes_bid, *no_bid);
  if (size <= 0.0) {
    return std::nullopt;
  }

  const double fee_estimate =
      total_taker_fee_usd(size, *yes_bid, *no_bid, market.taker_fee_rate);
  const double net_edge = gross_edge - (fee_estimate / size);
  if (net_edge < min_net_edge) {
    return std::nullopt;
  }

  ArbOpportunity opp;
  opp.market = market;
  opp.kind = ArbKind::SellBoth;
  opp.yes_price = *yes_bid;
  opp.no_price = *no_bid;
  opp.gross_edge = gross_edge;
  opp.fee_estimate = fee_estimate;
  opp.net_edge = net_edge;
  opp.max_size = size;
  opp.expected_profit_usd = net_edge * size;
  return opp;
}

}  // namespace pm
