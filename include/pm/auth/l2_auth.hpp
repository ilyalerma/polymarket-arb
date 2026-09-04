#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

namespace pm::auth {

std::map<std::string, std::string> build_l2_headers(
    const std::string& address,
    const std::string& api_key,
    const std::string& api_secret,
    const std::string& api_passphrase,
    const std::string& method,
    const std::string& path,
    const std::string& body = "");

std::map<std::string, std::string> build_l2_headers(
    const std::string& address,
    const std::string& api_key,
    const std::string& api_secret,
    const std::string& api_passphrase,
    const std::string& method,
    const std::string& path,
    const char* body,
    std::size_t body_len);

}  // namespace pm::auth
