#include "pm/market_tracker.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace pm {

namespace {

std::string market_kind_label(const BinaryMarket& market) {
  if (market.market_kind == MarketKind::Moneyline) {
    return "MONEYLINE";
  }
  if (market.market_kind == MarketKind::GameWinner) {
    return market.group_title.empty() ? "GAME_WINNER" : market.group_title;
  }
  return "BINARY";
}

std::string now_string() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
  return buf;
}

void print_game_finished(const BinaryMarket& market, const GameResolutionResult& resolution) {
  std::cout << "\n[" << now_string() << "] GAME FINISHED — "
            << market_kind_label(market) << " — " << resolution.winner_outcome
            << " won [" << market.slug << "]\n";
}

void print_now_watching(const BinaryMarket& market) {
  std::cout << "[" << now_string() << "] Now watching " << market_kind_label(market)
            << " [" << market.slug << "]\n";
}

void print_now_watching_moneyline(
    const std::string& event_slug,
    const std::string& event_title) {
  std::cout << "[" << now_string() << "] Now watching MONEYLINE (series decider)";
  if (!event_title.empty()) {
    std::cout << " — " << event_title;
  }
  std::cout << " [" << event_slug << "]\n";
}

bool watch_targets_equal(const EventWatchTarget& a, const EventWatchTarget& b) {
  return a.watch_moneyline == b.watch_moneyline && a.game_number == b.game_number;
}

}  // namespace

bool should_scan_for_arb(
    const Config& config,
    const BinaryMarket& market,
    const GameTracker& tracker) {
  if (!config.focus_current_game || market.event_slug.empty()) {
    if (market.market_kind == MarketKind::GameWinner) {
      const auto it = tracker.resolution_by_slug.find(market.slug);
      if (it != tracker.resolution_by_slug.end() && it->second != GameResolution::Open) {
        return false;
      }
    }
    return true;
  }

  const auto watch_it = tracker.watch_by_event.find(market.event_slug);
  if (watch_it == tracker.watch_by_event.end()) {
    return market.market_kind != MarketKind::Moneyline;
  }

  const auto& target = watch_it->second;

  if (market.market_kind == MarketKind::Moneyline) {
    return target.watch_moneyline;
  }

  if (market.market_kind == MarketKind::GameWinner) {
    if (target.watch_moneyline) {
      return false;
    }

    const auto it = tracker.resolution_by_slug.find(market.slug);
    if (it != tracker.resolution_by_slug.end() && it->second != GameResolution::Open) {
      return false;
    }

    const auto game_num = parse_game_number(market);
    return game_num && *game_num == target.game_number;
  }

  return true;
}

GameTracker build_game_tracker(
    const Config& config,
    const std::vector<BinaryMarket>& markets,
    const std::unordered_map<std::string, MarketBooks>& books_by_condition,
    GameTracker& previous) {
  GameTracker tracker;
  tracker.resolution_by_slug = previous.resolution_by_slug;
  tracker.watch_by_event = previous.watch_by_event;

  std::vector<MarketSnapshot> snapshots;
  snapshots.reserve(markets.size());

  for (const auto& market : markets) {
    const auto books_it = books_by_condition.find(market.condition_id);
    if (books_it == books_by_condition.end()) {
      continue;
    }

    MarketSnapshot snap;
    snap.market = market;
    snap.books = books_it->second;
    if (market.market_kind == MarketKind::GameWinner) {
      snap.resolution = detect_game_resolution(
          market,
          snap.books.yes,
          snap.books.no,
          config.game_loser_ask_max,
          config.game_winner_bid_min);
    }
    snapshots.push_back(snap);
  }

  std::unordered_map<std::string, std::unordered_map<std::uint32_t, GameResolution>>
      game_resolutions_by_event;
  std::unordered_map<std::string, std::unordered_set<std::uint32_t>> available_games_by_event;
  std::unordered_map<std::string, std::uint32_t> max_listed_game_by_event;
  std::unordered_map<std::string, std::string> event_title_by_slug;

  for (const auto& snap : snapshots) {
    if (!snap.market.event_slug.empty()) {
      event_title_by_slug[snap.market.event_slug] = snap.market.event_title;
      if (snap.market.max_listed_game > 0) {
        max_listed_game_by_event[snap.market.event_slug] = snap.market.max_listed_game;
      }
    }

    if (snap.market.market_kind != MarketKind::GameWinner) {
      continue;
    }

    const auto prior_it = tracker.resolution_by_slug.find(snap.market.slug);
    const auto prior =
        prior_it == tracker.resolution_by_slug.end() ? GameResolution::Open : prior_it->second;

    tracker.resolution_by_slug[snap.market.slug] = snap.resolution.state;

    if (prior == GameResolution::Open && snap.resolution.state != GameResolution::Open) {
      print_game_finished(snap.market, snap.resolution);
    }

    const auto game_num = parse_game_number(snap.market);
    if (!game_num) {
      continue;
    }

    available_games_by_event[snap.market.event_slug].insert(*game_num);
    game_resolutions_by_event[snap.market.event_slug][*game_num] = snap.resolution.state;
  }

  for (const auto& [event_slug, game_resolutions] : game_resolutions_by_event) {
    const std::uint32_t max_listed_game =
        max_listed_game_by_event.count(event_slug) ? max_listed_game_by_event[event_slug] : 0;
    const auto available_games = available_games_by_event[event_slug];
    const auto target =
        compute_sequential_watch_target(max_listed_game, game_resolutions, available_games);

    const auto prev_it = tracker.watch_by_event.find(event_slug);
    const bool had_previous = prev_it != tracker.watch_by_event.end();
    const auto previous_target = had_previous ? prev_it->second : EventWatchTarget{};

    tracker.watch_by_event[event_slug] = target;

    if (!config.focus_current_game ||
        (had_previous && watch_targets_equal(previous_target, target))) {
      continue;
    }

    if (target.watch_moneyline) {
      print_now_watching_moneyline(
          event_slug,
          event_title_by_slug.count(event_slug) ? event_title_by_slug[event_slug] : "");
      continue;
    }

    for (const auto& snap : snapshots) {
      if (snap.market.event_slug != event_slug ||
          snap.market.market_kind != MarketKind::GameWinner) {
        continue;
      }
      const auto num = parse_game_number(snap.market);
      if (num && *num == target.game_number) {
        print_now_watching(snap.market);
        break;
      }
    }
  }

  previous = tracker;
  return tracker;
}

}  // namespace pm
