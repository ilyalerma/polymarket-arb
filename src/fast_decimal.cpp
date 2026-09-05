#include "pm/fast_decimal.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace pm {

bool parse_double(std::string_view text, double& out) {
  if (text.empty()) {
    return false;
  }

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L && !defined(__APPLE__)
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, out);
  if (result.ec == std::errc{} && result.ptr == end) {
    return true;
  }
#endif

  char buffer[128];
  if (text.size() >= sizeof(buffer)) {
    return false;
  }
  std::memcpy(buffer, text.data(), text.size());
  buffer[text.size()] = '\0';

  char* parse_end = nullptr;
  out = std::strtod(buffer, &parse_end);
  if (parse_end == buffer) {
    return false;
  }
  return static_cast<std::size_t>(parse_end - buffer) == text.size();
}

}  // namespace pm
