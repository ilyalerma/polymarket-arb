#include "pm/game_state.hpp"

#include <regex>

namespace pm {

namespace {

bool loser_ask_collapsed(const std::optional<double>& ask, double loser_ask_max) {
  return ask && *ask <= loser_ask_max;
}

bool winner_bid_strong(const std::optional<double>& bid, double winner_bid_min) {
  return bid && *bid >= winner_bid_min;
}

}  // namespace

std::optional<std::uint32_t> parse_game_number(const BinaryMarket& market) {
  static const std::regex pattern(R"(^Game (\d+) Winner$)");
  std::smatch match;
  if (!std::regex_match(market.group_title, match, pattern)) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(std::stoul(match[1].str()));
}

std::optional<std::uint32_t> parse_series_best_of(
    const std::string& title,
    const std::string& score) {
  static const std::regex title_pattern(R"(\(BO(\d+)\))", std::regex::icase);
  std::smatch match;
  if (std::regex_search(title, match, title_pattern)) {
    return static_cast<std::uint32_t>(std::stoul(match[1].str()));
  }

  static const std::regex score_pattern(R"(Bo(\d+))", std::regex::icase);
  if (std::regex_search(score, match, score_pattern)) {
    return static_cast<std::uint32_t>(std::stoul(match[1].str()));
  }

  return std::nullopt;
}

EventWatchTarget compute_sequential_watch_target(
    std::uint32_t max_listed_game,
    const std::unordered_map<std::uint32_t, GameResolution>& game_resolutions,
    const std::unordered_set<std::uint32_t>& available_games) {
  std::uint32_t game_number = 1;
  const std::uint32_t upper_bound =
      max_listed_game > 0 ? max_listed_game + 1 : available_games.size() + 1;

  while (game_number <= upper_bound) {
    const auto resolution_it = game_resolutions.find(game_number);
    const bool resolved =
        resolution_it != game_resolutions.end() &&
        resolution_it->second != GameResolution::Open;
    const bool exists = available_games.count(game_number) > 0;

    if (!resolved && exists) {
      return {false, game_number};
    }
    if (!resolved && !exists) {
      return {true, 0};
    }
    ++game_number;
  }

  return {true, 0};
}

GameResolutionResult detect_game_resolution(
    const BinaryMarket& market,
    const OrderBook& yes_book,
    const OrderBook& no_book,
    double loser_ask_max,
    double winner_bid_min) {
  const auto yes_bid = yes_book.best_bid();
  const auto yes_ask = yes_book.best_ask();
  const auto no_bid = no_book.best_bid();
  const auto no_ask = no_book.best_ask();

  // Outcome one won: winner bid at ~0.99, loser offer collapses (~0.01).
  if (loser_ask_collapsed(no_ask, loser_ask_max) && winner_bid_strong(yes_bid, winner_bid_min)) {
    return {GameResolution::OutcomeOneWon, market.yes_outcome};
  }

  // Outcome two won.
  if (loser_ask_collapsed(yes_ask, loser_ask_max) && winner_bid_strong(no_bid, winner_bid_min)) {
    return {GameResolution::OutcomeTwoWon, market.no_outcome};
  }

  return {GameResolution::Open, {}};
}

}  // namespace pm
