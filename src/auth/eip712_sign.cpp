#include "pm/auth/eip712_sign.hpp"

#include <openssl/bn.h>

#include <secp256k1.h>
#include <secp256k1_recovery.h>

#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace pm::auth {

namespace {

constexpr char kDomainName[] = "Polymarket CTF Exchange";
constexpr char kDomainVersion[] = "2";
constexpr std::uint64_t kPolygonChainId = 137;
constexpr char kCtfExchange[] = "0xE111180000d2663C0091e4f400237545B87B996B";
constexpr char kNegRiskExchange[] = "0xe2222d279d744050d28e00520010520000310F59";
constexpr char kOrderTypeString[] =
    "Order(uint256 salt,address maker,address signer,uint256 tokenId,"
    "uint256 makerAmount,uint256 takerAmount,uint8 side,uint8 signatureType,"
    "uint256 timestamp,bytes32 metadata,bytes32 builder)";
constexpr char kZeroBytes32[] =
    "0x0000000000000000000000000000000000000000000000000000000000000000";

secp256k1_context* signing_context() {
  static secp256k1_context* ctx =
      secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
  return ctx;
}

std::string strip_hex_prefix(const std::string& hex) {
  if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
    return hex.substr(2);
  }
  return hex;
}

std::vector<std::uint8_t> hex_to_bytes(const std::string& hex) {
  const std::string stripped = strip_hex_prefix(hex);
  if (stripped.size() % 2 != 0) {
    throw std::runtime_error("invalid hex length");
  }
  std::vector<std::uint8_t> out;
  out.reserve(stripped.size() / 2);
  for (std::size_t i = 0; i < stripped.size(); i += 2) {
    const auto byte = std::stoul(stripped.substr(i, 2), nullptr, 16);
    out.push_back(static_cast<std::uint8_t>(byte));
  }
  return out;
}

std::string bytes_to_hex(const std::uint8_t* data, std::size_t len) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    out.push_back(digits[(data[i] >> 4) & 0x0f]);
    out.push_back(digits[data[i] & 0x0f]);
  }
  return out;
}

void append_word32(std::vector<std::uint8_t>& out, const Hash256& word) {
  out.insert(out.end(), word.begin(), word.end());
}

Hash256 word_from_u64(std::uint64_t value) {
  Hash256 out{};
  for (int i = 0; i < 8; ++i) {
    out[31 - i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xff);
  }
  return out;
}

Hash256 word_from_u256_string(const std::string& decimal) {
  BIGNUM* bn = BN_new();
  if (BN_dec2bn(&bn, decimal.c_str()) == 0) {
    BN_free(bn);
    throw std::runtime_error("invalid decimal for uint256");
  }
  Hash256 out{};
  const int num_bytes = BN_num_bytes(bn);
  if (num_bytes > 32) {
    BN_free(bn);
    throw std::runtime_error("uint256 overflow");
  }
  BN_bn2binpad(bn, out.data() + (32 - num_bytes), num_bytes);
  BN_free(bn);
  return out;
}

Hash256 word_from_address(const std::string& address_hex) {
  const auto bytes = hex_to_bytes(address_hex);
  if (bytes.size() != 20) {
    throw std::runtime_error("invalid address length");
  }
  Hash256 out{};
  std::memcpy(out.data() + 12, bytes.data(), 20);
  return out;
}

Hash256 word_from_bytes32(const std::string& bytes32_hex) {
  const auto bytes = hex_to_bytes(bytes32_hex);
  if (bytes.size() != 32) {
    throw std::runtime_error("invalid bytes32 length");
  }
  Hash256 out{};
  std::memcpy(out.data(), bytes.data(), 32);
  return out;
}

Hash256 domain_separator(bool neg_risk) {
  const Hash256 domain_type_hash = keccak256(
      "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)");
  const Hash256 name_hash = keccak256(kDomainName);
  const Hash256 version_hash = keccak256(kDomainVersion);
  const Hash256 chain_id = word_from_u64(kPolygonChainId);
  const Hash256 contract =
      word_from_address(neg_risk ? kNegRiskExchange : kCtfExchange);

  std::vector<std::uint8_t> encoded;
  encoded.reserve(32 * 5);
  append_word32(encoded, domain_type_hash);
  append_word32(encoded, name_hash);
  append_word32(encoded, version_hash);
  append_word32(encoded, chain_id);
  append_word32(encoded, contract);
  return keccak256(encoded);
}

Hash256 order_struct_hash(const OrderSignFields& order) {
  const Hash256 type_hash = keccak256(kOrderTypeString);
  const Hash256 metadata = word_from_bytes32(kZeroBytes32);
  const Hash256 builder = word_from_bytes32(kZeroBytes32);

  std::vector<std::uint8_t> encoded;
  encoded.reserve(32 * 12);
  append_word32(encoded, type_hash);
  append_word32(encoded, word_from_u64(order.salt));
  append_word32(encoded, word_from_address(order.maker));
  append_word32(encoded, word_from_address(order.signer));
  append_word32(encoded, word_from_u256_string(order.token_id));
  append_word32(encoded, word_from_u64(order.maker_amount));
  append_word32(encoded, word_from_u64(order.taker_amount));
  append_word32(encoded, word_from_u64(order.side));
  append_word32(encoded, word_from_u64(order.signature_type));
  append_word32(encoded, word_from_u64(static_cast<std::uint64_t>(order.timestamp_ms)));
  append_word32(encoded, metadata);
  append_word32(encoded, builder);
  return keccak256(encoded);
}

Hash256 eip712_digest(const Hash256& domain, const Hash256& struct_hash) {
  std::array<std::uint8_t, 2> prefix{0x19, 0x01};
  std::vector<std::uint8_t> encoded;
  encoded.reserve(66);
  encoded.insert(encoded.end(), prefix.begin(), prefix.end());
  encoded.insert(encoded.end(), domain.begin(), domain.end());
  encoded.insert(encoded.end(), struct_hash.begin(), struct_hash.end());
  return keccak256(encoded);
}

std::array<std::uint8_t, 32> load_private_key(const std::string& private_key_hex) {
  const auto key_bytes = hex_to_bytes(private_key_hex);
  if (key_bytes.size() != 32) {
    throw std::runtime_error("private key must be 32 bytes");
  }
  std::array<std::uint8_t, 32> out{};
  std::memcpy(out.data(), key_bytes.data(), 32);
  return out;
}

std::string sign_digest(const Hash256& digest, const std::array<std::uint8_t, 32>& private_key) {
  secp256k1_ecdsa_recoverable_signature signature;
  if (!secp256k1_ecdsa_sign_recoverable(
          signing_context(), &signature, digest.data(), private_key.data(), nullptr, nullptr)) {
    throw std::runtime_error("secp256k1 sign failed");
  }

  std::array<std::uint8_t, 64> compact{};
  int recovery_id = 0;
  if (!secp256k1_ecdsa_recoverable_signature_serialize_compact(
          signing_context(), compact.data(), &recovery_id, &signature)) {
    throw std::runtime_error("failed to serialize recoverable signature");
  }

  const std::uint8_t v = static_cast<std::uint8_t>(27 + recovery_id);
  std::string out = "0x";
  out += bytes_to_hex(compact.data(), 32);
  out += bytes_to_hex(compact.data() + 32, 32);
  out += bytes_to_hex(&v, 1);
  return out;
}

}  // namespace

std::string address_from_private_key(const std::string& private_key_hex) {
  const auto private_key = load_private_key(private_key_hex);
  secp256k1_pubkey pubkey;
  if (!secp256k1_ec_pubkey_create(signing_context(), &pubkey, private_key.data())) {
    throw std::runtime_error("failed to derive public key");
  }

  std::array<std::uint8_t, 65> uncompressed{};
  std::size_t output_len = uncompressed.size();
  if (!secp256k1_ec_pubkey_serialize(
          signing_context(),
          uncompressed.data(),
          &output_len,
          &pubkey,
          SECP256K1_EC_UNCOMPRESSED)) {
    throw std::runtime_error("failed to serialize public key");
  }

  const Hash256 hash = keccak256(uncompressed.data() + 1, 64);
  return "0x" + bytes_to_hex(hash.data() + 12, 20);
}

std::string sign_polymarket_order(
    const OrderSignFields& order,
    bool neg_risk,
    const std::string& private_key_hex) {
  const Hash256 domain = domain_separator(neg_risk);
  const Hash256 struct_hash = order_struct_hash(order);
  const Hash256 digest = eip712_digest(domain, struct_hash);
  const auto private_key = load_private_key(private_key_hex);
  return sign_digest(digest, private_key);
}

}  // namespace pm::auth
