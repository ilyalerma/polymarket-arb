#include "pm/gamma_client.hpp"
#include "pm/game_state.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <regex>
#include <stdexcept>

namespace pm {

namespace {

using json = nlohmann::json;

std::vector<std::string> parse_string_array(const json& value) {
  if (value.is_string()) {
    return json::parse(value.get<std::string>()).get<std::vector<std::string>>();
  }
  if (value.is_array()) {
    return value.get<std::vector<std::string>>();
  }
  return {};
}

double parse_double_field(const json& obj, const char* key, double fallback) {
  if (!obj.contains(key)) {
    return fallback;
  }
  const auto& value = obj.at(key);
  if (value.is_number()) {
    return value.get<double>();
  }
  if (value.is_string()) {
    return std::stod(value.get<std::string>());
  }
  return fallback;
}

std::string infer_category(const json& market) {
  if (market.contains("events") && market.at("events").is_array() &&
      !market.at("events").empty()) {
    const auto& event = market.at("events").at(0);
    if (event.contains("seriesSlug")) {
      const auto series = event.at("seriesSlug").get<std::string>();
      if (series == "league-of-legends" || series == "esports") {
        return "sports";
      }
    }
    if (event.contains("tags") && event.at("tags").is_array() && !event.at("tags").empty()) {
      const auto& tag = event.at("tags").at(0);
      if (tag.contains("slug")) {
        return tag.at("slug").get<std::string>();
      }
      if (tag.contains("label")) {
        return tag.at("label").get<std::string>();
      }
    }
  }
  return "sports";
}

double fee_rate_for_category(const std::string& category) {
  if (category == "crypto") {
    return 0.07;
  }
  if (category == "sports") {
    return 0.05;
  }
  if (category == "geopolitics" || category == "world") {
    return 0.0;
  }
  return 0.05;
}

bool is_moneyline_market(const json& market, const std::string& event_slug) {
  const std::string slug = market.value("slug", "");
  const std::string group = market.value("groupItemTitle", "");
  const std::string smt = market.value("sportsMarketType", "");
  return slug == event_slug || smt == "moneyline" || group == "Match Winner";
}

bool is_game_winner_market(const json& market) {
  const std::string group = market.value("groupItemTitle", "");
  const std::string smt = market.value("sportsMarketType", "");
  if (smt != "child_moneyline") {
    return false;
  }
  static const std::regex pattern(R"(^Game \d+ Winner$)");
  return std::regex_match(group, pattern);
}

MarketKind classify_market(const json& market, const std::string& event_slug) {
  if (is_moneyline_market(market, event_slug)) {
    return MarketKind::Moneyline;
  }
  if (is_game_winner_market(market)) {
    return MarketKind::GameWinner;
  }
  return MarketKind::Other;
}

std::optional<BinaryMarket> parse_market(
    const json& market,
    bool allow_closed,
    const std::string& event_slug = {},
    const std::string& event_title = {}) {
  if (!allow_closed) {
    if (!market.value("active", false) || market.value("closed", true)) {
      return std::nullopt;
    }
    if (!market.value("enableOrderBook", false) || !market.value("acceptingOrders", false)) {
      return std::nullopt;
    }
  } else if (!market.value("enableOrderBook", false)) {
    return std::nullopt;
  }

  const auto outcomes = parse_string_array(market.at("outcomes"));
  const auto token_ids = parse_string_array(market.at("clobTokenIds"));
  if (outcomes.size() != 2 || token_ids.size() != 2) {
    return std::nullopt;
  }

  std::size_t yes_idx = 0;
  std::size_t no_idx = 1;
  for (std::size_t i = 0; i < outcomes.size(); ++i) {
    const auto label = outcomes[i];
    if (label == "Yes" || label == "YES" || label == "yes") {
      yes_idx = i;
    } else if (label == "No" || label == "NO" || label == "no") {
      no_idx = i;
    }
  }

  BinaryMarket parsed;
  parsed.condition_id = market.value("conditionId", "");
  parsed.question = market.value("question", "");
  parsed.slug = market.value("slug", "");
  parsed.event_slug = event_slug;
  parsed.event_title = event_title;
  parsed.group_title = market.value("groupItemTitle", "");
  parsed.market_kind = event_slug.empty() ? MarketKind::Other
                                          : classify_market(market, event_slug);
  parsed.yes_token_id = token_ids[yes_idx];
  parsed.no_token_id = token_ids[no_idx];
  parsed.yes_outcome = outcomes[yes_idx];
  parsed.no_outcome = outcomes[no_idx];
  parsed.category = infer_category(market);
  parsed.neg_risk = market.value("negRisk", false);
  parsed.accepting_orders = market.value("acceptingOrders", false);
  parsed.tick_size = parse_double_field(market, "orderPriceMinTickSize", 0.01);
  parsed.min_order_size = parse_double_field(market, "orderMinSize", 5.0);
  parsed.liquidity = parse_double_field(market, "liquidityNum", 0.0);
  parsed.volume_24h = parse_double_field(market, "volume24hr", 0.0);
  parsed.taker_fee_rate = fee_rate_for_category(parsed.category);
  return parsed;
}

LolEvent parse_event(const json& event_json) {
  LolEvent event;
  event.slug = event_json.value("slug", "");
  event.title = event_json.value("title", "");
  event.live = event_json.value("live", false);
  event.series_best_of =
      parse_series_best_of(event.title, event_json.value("score", "")).value_or(0);

  if (!event_json.contains("markets")) {
    return event;
  }

  std::uint32_t max_game_number = 0;
  for (const auto& market : event_json.at("markets")) {
    if (!is_game_winner_market(market)) {
      continue;
    }
    pm::BinaryMarket probe;
    probe.group_title = market.value("groupItemTitle", "");
    const auto game_number = parse_game_number(probe);
    if (game_number) {
      max_game_number = std::max(max_game_number, *game_number);
    }
  }
  event.max_listed_game = max_game_number;

  for (const auto& market : event_json.at("markets")) {
    auto parsed = parse_market(market, false, event.slug, event.title);
    if (!parsed) {
      continue;
    }
    parsed->series_best_of = event.series_best_of;
    parsed->max_listed_game = event.max_listed_game;
    event.markets.push_back(*parsed);
  }
  return event;
}

}  // namespace

GammaClient::GammaClient(HttpClient&& http) : http_(std::move(http)) {}

std::optional<BinaryMarket> GammaClient::fetch_market_by_slug(
    const std::string& slug,
    const bool allow_closed) const {
  const auto response = http_.get("/markets", {{"slug", slug}});
  if (!response) {
    return std::nullopt;
  }

  const json markets = json::parse(*response);
  if (!markets.is_array() || markets.empty()) {
    return std::nullopt;
  }
  return parse_market(markets.at(0), allow_closed);
}

std::optional<BinaryMarket> GammaClient::fetch_market_from_event(
    const std::string& event_slug,
    const std::string& market_slug) const {
  const auto response = http_.get("/events", {{"slug", event_slug}});
  if (!response) {
    return std::nullopt;
  }

  const json events = json::parse(*response);
  if (!events.is_array() || events.empty() || !events.at(0).contains("markets")) {
    return std::nullopt;
  }

  const auto& event_json = events.at(0);
  const std::string event_title = event_json.value("title", "");
  for (const auto& market : event_json.at("markets")) {
    if (market.value("slug", "") != market_slug) {
      continue;
    }
    return parse_market(market, true, event_slug, event_title);
  }
  return std::nullopt;
}

std::vector<LolEvent> GammaClient::fetch_lol_events(
    const std::string& series_slug,
    std::uint32_t limit,
    bool live_only) const {
  const auto response = http_.get(
      "/events",
      {
          {"series_slug", series_slug},
          {"active", "true"},
          {"closed", "false"},
          {"limit", std::to_string(limit)},
          {"order", "startTime"},
          {"ascending", "false"},
      });
  if (!response) {
    return {};
  }

  const json events = json::parse(*response);
  std::vector<LolEvent> parsed;
  parsed.reserve(events.size());
  for (const auto& event_json : events) {
    if (live_only && !event_json.value("live", false)) {
      continue;
    }
    auto event = parse_event(event_json);
    if (!event.markets.empty()) {
      parsed.push_back(std::move(event));
    }
  }
  return parsed;
}

std::optional<LolEvent> GammaClient::fetch_lol_event(const std::string& event_slug) const {
  const auto response = http_.get("/events", {{"slug", event_slug}});
  if (!response) {
    return std::nullopt;
  }

  const json events = json::parse(*response);
  if (!events.is_array() || events.empty()) {
    return std::nullopt;
  }
  return parse_event(events.at(0));
}

std::vector<BinaryMarket> GammaClient::extract_arb_markets(
    const LolEvent& event,
    bool include_moneyline,
    bool include_game_winners) const {
  std::vector<BinaryMarket> markets;
  for (const auto& market : event.markets) {
    if (market.market_kind == MarketKind::Moneyline && include_moneyline) {
      markets.push_back(market);
    } else if (market.market_kind == MarketKind::GameWinner && include_game_winners) {
      markets.push_back(market);
    }
  }
  std::sort(
      markets.begin(),
      markets.end(),
      [](const BinaryMarket& lhs, const BinaryMarket& rhs) {
        if (lhs.market_kind != rhs.market_kind) {
          return lhs.market_kind == MarketKind::Moneyline;
        }
        return lhs.group_title < rhs.group_title;
      });
  return markets;
}

std::vector<BinaryMarket> GammaClient::fetch_active_markets(std::uint32_t limit) const {
  const auto response = http_.get(
      "/markets",
      {
          {"closed", "false"},
          {"limit", std::to_string(limit)},
          {"order", "volume24hr"},
          {"ascending", "false"},
      });
  if (!response) {
    return {};
  }

  const json markets = json::parse(*response);
  std::vector<BinaryMarket> parsed;
  parsed.reserve(markets.size());
  for (const auto& market : markets) {
    auto binary = parse_market(market, false);
    if (!binary) {
      continue;
    }
    parsed.push_back(*binary);
  }
  return parsed;
}

}  // namespace pm
