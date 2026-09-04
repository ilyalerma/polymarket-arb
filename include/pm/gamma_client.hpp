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

 private:
  HttpClient http_;
};

}  // namespace pm
