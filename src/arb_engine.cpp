#include "pm/arb_engine.hpp"

#include <algorithm>

namespace pm {

ArbEngine::ArbEngine(Config config, GammaClient gamma, ClobClient clob)
    : config_(std::move(config)),
      gamma_(std::move(gamma)),
      clob_(std::move(clob)) {
  markets_ = gamma_.fetch_active_markets(config_.market_limit);
  markets_.erase(
      std::remove_if(
          markets_.begin(),
          markets_.end(),
          [&](const BinaryMarket& market) {
            return market.liquidity < config_.min_liquidity;
          }),
      markets_.end());

  for (const auto& market : markets_) {
    token_to_market_[market.yes_token_id] = market;
    token_to_market_[market.no_token_id] = market;
  }
}

std::vector<ArbOpportunity> ArbEngine::scan_once() {
  std::vector<ArbOpportunity> opportunities;
  for (const auto& market : markets_) {
    const auto yes_book = clob_.fetch_book(market.yes_token_id);
    const auto no_book = clob_.fetch_book(market.no_token_id);
    if (!yes_book || !no_book) {
      continue;
    }

    MarketBooks books;
    books.yes.update(*yes_book);
    books.no.update(*no_book);
    on_market_books(
        market,
        books,
        [&](const ArbOpportunity& opp) { opportunities.push_back(opp); });
  }
  std::sort(
      opportunities.begin(),
      opportunities.end(),
      [](const ArbOpportunity& lhs, const ArbOpportunity& rhs) {
        return lhs.expected_profit_usd > rhs.expected_profit_usd;
      });
  return opportunities;
}

void ArbEngine::on_market_books(
    const BinaryMarket& market,
    const MarketBooks& books,
    const std::function<void(const ArbOpportunity&)>& on_arb) {
  if (auto buy = detect_buy_both_arb(
          market,
          books.yes,
          books.no,
          config_.min_net_edge,
          config_.max_trade_usd)) {
    on_arb(*buy);
  }
  if (auto sell = detect_sell_both_arb(
          market,
          books.yes,
          books.no,
          config_.min_net_edge,
          config_.max_trade_usd)) {
    on_arb(*sell);
  }
}

}  // namespace pm
