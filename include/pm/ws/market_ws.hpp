#pragma once

#include "pm/clob_client.hpp"
#include "pm/market_tracker.hpp"
#include "pm/order_chamber.hpp"
#include "pm/types.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pm {

class MarketWs {
 public:
  using ArbCallback = std::function<void(const ArbOpportunity&)>;

  MarketWs(
      Config config,
      std::vector<BinaryMarket> markets,
      ClobClient* clob,
      OrderChamberRegistry* chambers,
      ArbCallback on_arb);
  ~MarketWs();

  void start();
  void stop();
  void set_markets(const std::vector<BinaryMarket>& markets);

 private:
  void run();
  void handle_message(const std::string& message);
  void rebuild_indexes();
  std::string build_subscribe_message() const;
  void process_updates_for_event(const std::string& event_slug);
  void check_arb_for_market(const BinaryMarket& market);
  bool market_books_ready(const BinaryMarket& market) const;

  Config config_;
  std::vector<BinaryMarket> markets_;
  ClobClient* clob_{nullptr};
  OrderChamberRegistry* chambers_{nullptr};
  ArbCallback on_arb_;
  GameTracker game_tracker_;

  std::unordered_map<std::string, MarketBooks> books_;
  std::unordered_map<std::string, BinaryMarket> token_to_market_;
  std::unordered_map<std::string, BinaryMarket> condition_to_market_;
  std::unordered_map<std::string, std::vector<BinaryMarket>> markets_by_event_;
  std::unordered_set<std::string> tokens_with_book_;

  std::atomic<bool> running_{false};
  std::atomic<bool> markets_dirty_{false};
  mutable std::mutex markets_mutex_;
  std::thread thread_;
};

}  // namespace pm
