#pragma once

#include "pm/order_book.hpp"
#include "pm/types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace pm {

enum class GameResolution { Open, OutcomeOneWon, OutcomeTwoWon };

struct GameResolutionResult {
  GameResolution state{GameResolution::Open};
  std::string winner_outcome;
};

std::optional<std::uint32_t> parse_game_number(const BinaryMarket& market);

GameResolutionResult detect_game_resolution(
    const BinaryMarket& market,
    const OrderBook& yes_book,
    const OrderBook& no_book,
    double loser_bid_max,
    double winner_ask_min);

}  // namespace pm
