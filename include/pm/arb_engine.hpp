#pragma once

#include "pm/clob_client.hpp"
#include "pm/config.hpp"
#include "pm/fee_model.hpp"
#include "pm/gamma_client.hpp"
#include "pm/types.hpp"

#include <functional>
#include <unordered_map>

namespace pm {

class ArbEngine {
 public:
  ArbEngine(Config config, GammaClient gamma, ClobClient clob);

  std::vector<ArbOpportunity> scan_once();
  void on_market_books(
      const BinaryMarket& market,
      const MarketBooks& books,
      const std::function<void(const ArbOpportunity&)>& on_arb);

 private:
  Config config_;
  GammaClient gamma_;
  ClobClient clob_;
  std::vector<BinaryMarket> markets_;
  std::unordered_map<std::string, BinaryMarket> token_to_market_;
};

}  // namespace pm
