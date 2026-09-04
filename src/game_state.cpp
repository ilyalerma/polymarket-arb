#include "pm/game_state.hpp"

#include <regex>

namespace pm {

namespace {

bool ask_gone_or_expensive(const std::optional<double>& ask, double winner_ask_min) {
  return !ask || *ask >= winner_ask_min;
}

bool loser_bid_collapsed(const std::optional<double>& bid, double loser_bid_max) {
  return bid && *bid <= loser_bid_max;
}

bool winner_bid_strong(const std::optional<double>& bid, double winner_ask_min) {
  return bid && *bid >= winner_ask_min - 0.05;
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

GameResolutionResult detect_game_resolution(
    const BinaryMarket& market,
    const OrderBook& yes_book,
    const OrderBook& no_book,
    double loser_bid_max,
    double winner_ask_min) {
  const auto yes_bid = yes_book.best_bid();
  const auto yes_ask = yes_book.best_ask();
  const auto no_bid = no_book.best_bid();
  const auto no_ask = no_book.best_ask();

  // Outcome one won: winner asks disappear / go expensive, loser bid collapses (~0.10).
  if (loser_bid_collapsed(no_bid, loser_bid_max) &&
      (ask_gone_or_expensive(yes_ask, winner_ask_min) || winner_bid_strong(yes_bid, winner_ask_min))) {
    return {GameResolution::OutcomeOneWon, market.yes_outcome};
  }

  // Outcome two won.
  if (loser_bid_collapsed(yes_bid, loser_bid_max) &&
      (ask_gone_or_expensive(no_ask, winner_ask_min) || winner_bid_strong(no_bid, winner_ask_min))) {
    return {GameResolution::OutcomeTwoWon, market.no_outcome};
  }

  return {GameResolution::Open, {}};
}

}  // namespace pm
