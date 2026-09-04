#pragma once

#include "pm/order_book.hpp"
#include "pm/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace pm {

enum class GameResolution { Open, OutcomeOneWon, OutcomeTwoWon };

struct GameResolutionResult {
  GameResolution state{GameResolution::Open};
  std::string winner_outcome;
};

struct EventWatchTarget {
  bool watch_moneyline{false};
  std::uint32_t game_number{0};
};

std::optional<std::uint32_t> parse_game_number(const BinaryMarket& market);
std::optional<std::uint32_t> parse_series_best_of(
    const std::string& title,
    const std::string& score);

GameResolutionResult detect_game_resolution(
    const BinaryMarket& market,
    const OrderBook& yes_book,
    const OrderBook& no_book,
    double loser_ask_max,
    double winner_bid_min);

EventWatchTarget compute_sequential_watch_target(
    std::uint32_t max_listed_game,
    const std::unordered_map<std::uint32_t, GameResolution>& game_resolutions,
    const std::unordered_set<std::uint32_t>& available_games);

}  // namespace pm
