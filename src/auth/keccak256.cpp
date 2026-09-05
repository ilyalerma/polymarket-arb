#include "pm/auth/keccak256.hpp"

#include <cstring>

namespace pm::auth {

namespace {

constexpr std::uint64_t keccakf_rndc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

constexpr int keccakf_rotc[24] = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14, 27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};

constexpr int keccakf_piln[24] = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4, 15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};

void keccakf(std::uint64_t st[25]) {
  for (int round = 0; round < 24; ++round) {
    std::uint64_t bc[5];
    for (int i = 0; i < 5; ++i) {
      bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];
    }
    for (int i = 0; i < 5; ++i) {
      const std::uint64_t t = bc[(i + 4) % 5] ^ ((bc[(i + 1) % 5] << 1) | (bc[(i + 1) % 5] >> 63));
      for (int j = 0; j < 25; j += 5) {
        st[j + i] ^= t;
      }
    }
    std::uint64_t t = st[1];
    for (int i = 0; i < 24; ++i) {
      const int j = keccakf_piln[i];
      bc[0] = st[j];
      st[j] = (t << keccakf_rotc[i]) | (t >> (64 - keccakf_rotc[i]));
      t = bc[0];
    }
    for (int j = 0; j < 25; j += 5) {
      for (int i = 0; i < 5; ++i) {
        bc[i] = st[j + i];
      }
      for (int i = 0; i < 5; ++i) {
        st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
      }
    }
    st[0] ^= keccakf_rndc[round];
  }
}

Hash256 keccak256_impl(const void* data, std::size_t len) {
  std::uint64_t st[25]{};
  constexpr std::size_t rate = 136;  // 1088-bit rate for Keccak-256
  const auto* input = static_cast<const std::uint8_t*>(data);
  std::size_t offset = 0;

  while (len >= rate) {
    for (std::size_t i = 0; i < rate / 8; ++i) {
      std::uint64_t word = 0;
      std::memcpy(&word, input + offset + i * 8, 8);
      st[i] ^= word;
    }
    keccakf(st);
    offset += rate;
    len -= rate;
  }

  std::uint8_t temp[rate]{};
  if (len > 0) {
    std::memcpy(temp, input + offset, len);
  }
  temp[len] = 0x01;
  temp[rate - 1] |= 0x80;

  for (std::size_t i = 0; i < rate / 8; ++i) {
    std::uint64_t word = 0;
    std::memcpy(&word, temp + i * 8, 8);
    st[i] ^= word;
  }
  keccakf(st);

  Hash256 out{};
  std::memcpy(out.data(), st, 32);
  return out;
}

}  // namespace

Hash256 keccak256(const void* data, std::size_t len) {
  return keccak256_impl(data, len);
}

Hash256 keccak256(const std::vector<std::uint8_t>& data) {
  return keccak256_impl(data.data(), data.size());
}

Hash256 keccak256(const std::string& data) {
  return keccak256_impl(data.data(), data.size());
}

}  // namespace pm::auth
