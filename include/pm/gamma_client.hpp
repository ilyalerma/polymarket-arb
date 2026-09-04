#pragma once

#include "pm/http_client.hpp"
#include "pm/types.hpp"

#include <optional>
#include <vector>

namespace pm {

class GammaClient {
 public:
  explicit GammaClient(HttpClient&& http);

  std::vector<BinaryMarket> fetch_active_markets(std::uint32_t limit) const;
  std::optional<BinaryMarket> fetch_market_by_slug(
      const std::string& slug,
      bool allow_closed = false) const;
  std::optional<BinaryMarket> fetch_market_from_event(
      const std::string& event_slug,
      const std::string& market_slug) const;

  std::vector<LolEvent> fetch_lol_events(
      const std::string& series_slug,
      std::uint32_t limit,
      bool live_only) const;
  std::optional<LolEvent> fetch_lol_event(const std::string& event_slug) const;
  std::vector<BinaryMarket> extract_arb_markets(
      const LolEvent& event,
      bool include_moneyline,
      bool include_game_winners) const;

 private:
  HttpClient http_;
};

}  // namespace pm
