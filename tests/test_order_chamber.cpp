#include "pm/order_chamber.hpp"
#include "pm/types.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

namespace {

bool expect_true(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

pm::BinaryMarket make_market() {
  pm::BinaryMarket market;
  market.slug = "lol-test-game1";
  market.yes_token_id = "111";
  market.no_token_id = "222";
  market.min_order_size = 5.0;
  return market;
}

pm::Config make_config() {
  pm::Config config;
  config.wallet_address = "0x00000000000000000000000000000000000000aa";
  config.api_key = "api-key-owner";
  return config;
}

}  // namespace

int main() {
  int failures = 0;

  const auto buy_amounts = pm::compute_order_amounts(pm::OrderSide::Buy, 10.0, 0.45);
  failures += !expect_true(
      buy_amounts.maker_amount == "4500000" && buy_amounts.taker_amount == "10000000",
      "buy amount conversion failed");

  const auto sell_amounts = pm::compute_order_amounts(pm::OrderSide::Sell, 10.0, 0.55);
  failures += !expect_true(
      sell_amounts.maker_amount == "10000000" && sell_amounts.taker_amount == "5500000",
      "sell amount conversion failed");

  const auto market = make_market();
  const auto config = make_config();

  pm::OrderChamber chamber;
  chamber.prime(market, config, pm::ArbKind::BuyBoth);

  pm::ArbOpportunity opp;
  opp.market = market;
  opp.kind = pm::ArbKind::BuyBoth;
  opp.yes_price = 0.45;
  opp.no_price = 0.54;
  opp.max_size = 12.0;
  chamber.aim(opp);

  failures += !expect_true(chamber.ready(), "chamber should be ready after aim");

  pm::FiredShot shot;
  failures += !expect_true(
      chamber.fire_into(shot, 12.0, 42, 1'713'398'400'000),
      "fire_into failed");
  failures += !expect_true(shot.yes.size > 0 && shot.no.size > 0, "fire returned empty bodies");

  const auto yes_json = nlohmann::json::parse(shot.yes.view());
  failures += !expect_true(yes_json["order"]["salt"] == "42", "salt not set");
  failures += !expect_true(yes_json["order"]["tokenId"] == "111", "yes token id mismatch");
  failures += !expect_true(yes_json["order"]["makerAmount"] == "5400000", "yes maker amount mismatch");
  failures += !expect_true(yes_json["order"]["takerAmount"] == "12000000", "yes taker amount mismatch");
  failures += !expect_true(yes_json["order"]["side"] == "BUY", "yes side mismatch");
  failures += !expect_true(yes_json["orderType"] == "FOK", "order type mismatch");

  const auto no_json = nlohmann::json::parse(shot.no.view());
  failures += !expect_true(no_json["order"]["salt"] == "43", "no leg salt should differ");
  failures += !expect_true(no_json["order"]["tokenId"] == "222", "no token id mismatch");
  failures += !expect_true(no_json["order"]["makerAmount"] == "6480000", "no maker amount mismatch");

  pm::OrderChamberRegistry registry;
  registry.prime_all({market}, config);
  const auto* buy_chamber = registry.chamber_for(market.slug, pm::ArbKind::BuyBoth);
  const auto* sell_chamber = registry.chamber_for(market.slug, pm::ArbKind::SellBoth);
  failures += !expect_true(buy_chamber != nullptr && sell_chamber != nullptr, "registry lookup failed");

  if (failures == 0) {
    std::cout << "order chamber tests passed\n";
    return 0;
  }

  std::cerr << failures << " order chamber test(s) failed\n";
  return 1;
}
