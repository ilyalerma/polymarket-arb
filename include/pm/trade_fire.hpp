#pragma once

#include "pm/clob_client.hpp"
#include "pm/order_chamber.hpp"
#include "pm/types.hpp"

namespace pm {

void try_fire_arb(
    const Config& config,
    ClobClient& clob,
    OrderChamberRegistry& chambers,
    const ArbOpportunity& opp);

bool fire_test_buy(
    const Config& config,
    ClobClient& clob,
    const BinaryMarket& market,
    bool buy_yes,
    double size_shares,
    double limit_price);

}  // namespace pm
