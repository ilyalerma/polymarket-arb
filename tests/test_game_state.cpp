#include "pm/game_state.hpp"
#include "pm/market_tracker.hpp"
#include "pm/order_book.hpp"
#include "pm/types.hpp"

#include <iostream>
#include <unordered_map>
#include <unordered_set>

namespace {

pm::MarketBooks make_books(
    double yes_bid,
    double yes_ask,
    double no_bid,
    double no_ask) {
  pm::TokenBook yes;
  yes.bids = {{yes_bid, 1000.0}};
  yes.asks = {{yes_ask, 1000.0}};
  pm::TokenBook no;
  no.bids = {{no_bid, 1000.0}};
  no.asks = {{no_ask, 1000.0}};
  pm::MarketBooks books;
  books.yes.update(yes);
  books.no.update(no);
  return books;
}

}  // namespace

int main() {
  pm::BinaryMarket market;
  market.yes_outcome = "KT Rolster";
  market.no_outcome = "Dplus KIA";
  market.group_title = "Game 1 Winner";

  const auto game_num = pm::parse_game_number(market);
  if (!game_num || *game_num != 1) {
    std::cerr << "parse_game_number failed\n";
    return 1;
  }

  auto open = make_books(0.44, 0.45, 0.55, 0.56);
  const auto open_result = pm::detect_game_resolution(market, open.yes, open.no, 0.01, 0.99);
  if (open_result.state != pm::GameResolution::Open) {
    std::cerr << "expected open market\n";
    return 1;
  }

  // KT won: yes bid at 0.99, DK offer collapsed to 0.01
  auto kt_won = make_books(0.99, 0.99, 0.99, 0.01);
  const auto kt_result = pm::detect_game_resolution(market, kt_won.yes, kt_won.no, 0.01, 0.99);
  if (kt_result.state != pm::GameResolution::OutcomeOneWon ||
      kt_result.winner_outcome != "KT Rolster") {
    std::cerr << "expected KT win detection\n";
    return 1;
  }

  // DK won: no bid at 0.99, KT offer at 0.01
  auto dk_won = make_books(0.99, 0.01, 0.99, 0.99);
  const auto dk_result = pm::detect_game_resolution(market, dk_won.yes, dk_won.no, 0.01, 0.99);
  if (dk_result.state != pm::GameResolution::OutcomeTwoWon ||
      dk_result.winner_outcome != "Dplus KIA") {
    std::cerr << "expected DK win detection\n";
    return 1;
  }

  const auto bo3 = pm::parse_series_best_of("LoL: KT vs DK (BO3)", "Bo3");
  const auto bo5 = pm::parse_series_best_of("LoL: IG vs TES (BO5)", "");
  if (!bo3 || *bo3 != 3 || !bo5 || *bo5 != 5) {
    std::cerr << "parse_series_best_of failed\n";
    return 1;
  }

  std::unordered_map<std::uint32_t, pm::GameResolution> resolutions;
  std::unordered_set<std::uint32_t> available{1, 2};

  auto watch = pm::compute_sequential_watch_target(2, resolutions, available);
  if (watch.watch_moneyline || watch.game_number != 1) {
    std::cerr << "expected watch game 1 first\n";
    return 1;
  }

  resolutions[1] = pm::GameResolution::OutcomeOneWon;
  watch = pm::compute_sequential_watch_target(2, resolutions, available);
  if (watch.watch_moneyline || watch.game_number != 2) {
    std::cerr << "expected watch game 2 after game 1 resolved\n";
    return 1;
  }

  resolutions[2] = pm::GameResolution::OutcomeTwoWon;
  watch = pm::compute_sequential_watch_target(2, resolutions, available);
  if (!watch.watch_moneyline) {
    std::cerr << "expected moneyline after BO3 games 1-2 resolved\n";
    return 1;
  }

  resolutions.clear();
  available = {1, 2, 3, 4};
  watch = pm::compute_sequential_watch_target(4, resolutions, available);
  if (watch.watch_moneyline || watch.game_number != 1) {
    std::cerr << "expected watch game 1 in BO5\n";
    return 1;
  }

  resolutions[1] = pm::GameResolution::OutcomeOneWon;
  resolutions[2] = pm::GameResolution::OutcomeTwoWon;
  resolutions[3] = pm::GameResolution::OutcomeOneWon;
  resolutions[4] = pm::GameResolution::OutcomeTwoWon;
  watch = pm::compute_sequential_watch_target(4, resolutions, available);
  if (!watch.watch_moneyline) {
    std::cerr << "expected moneyline for BO5 game 5 decider\n";
    return 1;
  }

  pm::Config config;
  config.focus_current_game = true;
  pm::GameTracker tracker;
  pm::BinaryMarket game1;
  game1.slug = "lol-test-event-game1";
  game1.event_slug = "lol-test-event";
  game1.condition_id = "cond-game1";
  game1.market_kind = pm::MarketKind::GameWinner;
  game1.group_title = "Game 1 Winner";
  game1.yes_outcome = "Team A";
  game1.no_outcome = "Team B";
  game1.max_listed_game = 2;

  std::unordered_map<std::string, pm::MarketBooks> books;
  books[game1.condition_id] = make_books(0.44, 0.45, 0.55, 0.56);

  tracker = pm::build_game_tracker(config, {game1}, books, tracker);
  if (!pm::should_scan_for_arb(config, game1, tracker)) {
    std::cerr << "expected open game 1 to be scanned\n";
    return 1;
  }

  books[game1.condition_id] = make_books(0.99, 0.99, 0.99, 0.01);
  tracker = pm::build_game_tracker(config, {game1}, books, tracker);
  if (pm::should_scan_for_arb(config, game1, tracker)) {
    std::cerr << "expected finished game 1 to stop scanning\n";
    return 1;
  }

  books[game1.condition_id] = make_books(0.44, 0.45, 0.55, 0.56);
  tracker = pm::build_game_tracker(config, {game1}, books, tracker);
  if (pm::should_scan_for_arb(config, game1, tracker)) {
    std::cerr << "expected finished game to stay finished after book reopens\n";
    return 1;
  }

  std::cout << "game state tests passed\n";
  return 0;
}
