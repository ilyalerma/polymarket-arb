#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace pm::fast {

inline char* write_u64(char* out, std::uint64_t value) {
  char tmp[20];
  std::size_t len = 0;
  do {
    tmp[len++] = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value != 0);

  while (len > 0) {
    *out++ = tmp[--len];
  }
  return out;
}

inline char* write_i64(char* out, std::int64_t value) {
  if (value < 0) {
    *out++ = '-';
    value = -value;
  }
  return write_u64(out, static_cast<std::uint64_t>(value));
}

inline char* append(char* out, std::string_view text) {
  std::memcpy(out, text.data(), text.size());
  return out + text.size();
}

}  // namespace pm::fast
