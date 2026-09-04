#pragma once

#include "pm/order_book.hpp"
#include "pm/types.hpp"

namespace pm {

double taker_fee_usd(double shares, double price, double fee_rate);
double total_taker_fee_usd(double shares, double yes_price, double no_price, double fee_rate);

std::optional<ArbOpportunity> detect_buy_both_arb(
    const BinaryMarket& market,
    const OrderBook& yes_book,
    const OrderBook& no_book,
    double min_net_edge,
    double max_trade_usd);

std::optional<ArbOpportunity> detect_sell_both_arb(
    const BinaryMarket& market,
    const OrderBook& yes_book,
    const OrderBook& no_book,
    double min_net_edge,
    double max_trade_usd);

}  // namespace pm
