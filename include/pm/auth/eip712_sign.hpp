#pragma once

#include "pm/auth/keccak256.hpp"

#include <cstdint>
#include <string>

namespace pm::auth {

struct OrderSignFields {
  std::uint64_t salt{0};
  std::string maker;
  std::string signer;
  std::string taker;
  std::string token_id;
  std::uint64_t maker_amount{0};
  std::uint64_t taker_amount{0};
  std::uint64_t expiration{0};
  std::uint64_t nonce{0};
  std::uint64_t fee_rate_bps{0};
  std::uint8_t side{0};  // 0 = BUY, 1 = SELL
  std::uint8_t signature_type{0};
};

// Returns 0x-prefixed 65-byte ECDSA signature (r||s||v).
std::string sign_polymarket_order(
    const OrderSignFields& order,
    bool neg_risk,
    const std::string& private_key_hex);

std::string address_from_private_key(const std::string& private_key_hex);

}  // namespace pm::auth
