#include "pm/game_state.hpp"
#include "pm/order_book.hpp"
#include "pm/types.hpp"

#include <iostream>

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
  const auto open_result = pm::detect_game_resolution(market, open.yes, open.no, 0.12, 0.95);
  if (open_result.state != pm::GameResolution::Open) {
    std::cerr << "expected open market\n";
    return 1;
  }

  // KT won: yes ask gone/expired, DK bid collapsed to 0.10
  auto kt_won = make_books(0.99, 0.99, 0.10, 0.99);
  const auto kt_result = pm::detect_game_resolution(market, kt_won.yes, kt_won.no, 0.12, 0.95);
  if (kt_result.state != pm::GameResolution::OutcomeOneWon ||
      kt_result.winner_outcome != "KT Rolster") {
    std::cerr << "expected KT win detection\n";
    return 1;
  }

  // DK won: no ask expensive, KT bid at 0.08
  auto dk_won = make_books(0.08, 0.99, 0.99, 0.99);
  const auto dk_result = pm::detect_game_resolution(market, dk_won.yes, dk_won.no, 0.12, 0.95);
  if (dk_result.state != pm::GameResolution::OutcomeTwoWon ||
      dk_result.winner_outcome != "Dplus KIA") {
    std::cerr << "expected DK win detection\n";
    return 1;
  }

  std::cout << "game state tests passed\n";
  return 0;
}
