#pragma once

#include "pm/arb_engine.hpp"
#include "pm/types.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace pm {

class MarketWs {
 public:
  using ArbCallback = std::function<void(const ArbOpportunity&)>;

  MarketWs(
      Config config,
      std::vector<BinaryMarket> markets,
      ArbCallback on_arb);
  ~MarketWs();

  void start();
  void stop();

 private:
  void run();
  void handle_message(const std::string& message);
  void refresh_books_for_market(const BinaryMarket& market);

  Config config_;
  std::vector<BinaryMarket> markets_;
  ArbCallback on_arb_;
  std::unordered_map<std::string, MarketBooks> books_;
  std::unordered_map<std::string, BinaryMarket> token_to_market_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace pm
