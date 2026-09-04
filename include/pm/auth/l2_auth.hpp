#pragma once

#include <map>
#include <string>

namespace pm::auth {

std::map<std::string, std::string> build_l2_headers(
    const std::string& address,
    const std::string& api_key,
    const std::string& api_secret,
    const std::string& api_passphrase,
    const std::string& method,
    const std::string& path,
    const std::string& body = "");

}  // namespace pm::auth
