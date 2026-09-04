#pragma once

#include "pm/config.hpp"
#include "pm/game_state.hpp"
#include "pm/order_book.hpp"
#include "pm/types.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pm {

struct MarketSnapshot {
  BinaryMarket market;
  MarketBooks books;
  GameResolutionResult resolution;
};

struct GameTracker {
  std::unordered_map<std::string, GameResolution> resolution_by_slug;
  std::unordered_map<std::string, EventWatchTarget> watch_by_event;
};

bool should_scan_for_arb(
    const Config& config,
    const BinaryMarket& market,
    const GameTracker& tracker);

GameTracker build_game_tracker(
    const Config& config,
    const std::vector<BinaryMarket>& markets,
    const std::unordered_map<std::string, MarketBooks>& books_by_condition,
    GameTracker& previous);

}  // namespace pm
