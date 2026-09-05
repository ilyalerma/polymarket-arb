#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pm::auth {

using Hash256 = std::array<std::uint8_t, 32>;

Hash256 keccak256(const void* data, std::size_t len);
Hash256 keccak256(const std::vector<std::uint8_t>& data);
Hash256 keccak256(const std::string& data);

}  // namespace pm::auth
