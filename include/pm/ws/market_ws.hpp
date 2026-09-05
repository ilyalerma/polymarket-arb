#pragma once

#include "pm/clob_client.hpp"
#include "pm/market_tracker.hpp"
#include "pm/order_chamber.hpp"
#include "pm/spsc_ring.hpp"
#include "pm/types.hpp"
#include "pm/ws/ws_book_event.hpp"

#include <simdjson.h>

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
  static constexpr std::size_t kEventRingCapacity = 4096;

  using ArbCallback = std::function<void(const ArbOpportunity&)>;
  using StatusCallback = std::function<void(const BinaryMarket&, const MarketBooks&)>;
  using EventRing = SpscRing<ws::WsBookEvent, kEventRingCapacity>;

  MarketWs(
      Config config,
      std::vector<BinaryMarket> markets,
      ClobClient* clob,
      OrderChamberRegistry* chambers,
      ArbCallback on_arb,
      StatusCallback on_status = nullptr);
  ~MarketWs();

  void start();
  void stop();
  void set_markets(const std::vector<BinaryMarket>& markets);

 private:
  void run();
  void arb_worker();
  void ingest_message(std::string_view message);
  void rebuild_indexes();
  std::string build_subscribe_message() const;
  void process_updates_for_event(const std::string& event_slug);
  void check_arb_for_market(const BinaryMarket& market);
  bool market_books_ready(const BinaryMarket& market) const;
  bool push_event(const ws::WsBookEvent& event);
  std::string event_key_for_market(const BinaryMarket& market) const;

  Config config_;
  std::vector<BinaryMarket> markets_;
  ClobClient* clob_{nullptr};
  OrderChamberRegistry* chambers_{nullptr};
  ArbCallback on_arb_;
  StatusCallback on_status_;
  GameTracker game_tracker_;

  std::unordered_map<std::string, MarketBooks> books_;
  std::unordered_map<std::string, BinaryMarket> token_to_market_;
  std::unordered_map<std::string, BinaryMarket> condition_to_market_;
  std::unordered_map<std::string, std::vector<BinaryMarket>> markets_by_event_;
  std::unordered_set<std::string> tokens_with_book_;

  EventRing event_ring_;
  simdjson::dom::parser json_parser_;
  std::atomic<bool> running_{false};
  std::atomic<bool> markets_dirty_{false};
  std::atomic<bool> ws_connected_{false};
  std::atomic<bool> subscribe_pending_{true};
  mutable std::mutex markets_mutex_;
  std::thread ws_thread_;
  std::thread arb_thread_;
};

}  // namespace pm
