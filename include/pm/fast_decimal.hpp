#pragma once

#include <string_view>

namespace pm {

bool parse_double(std::string_view text, double& out);

}  // namespace pm
