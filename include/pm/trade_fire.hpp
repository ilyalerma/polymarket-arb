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

}  // namespace pm
