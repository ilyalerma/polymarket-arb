#pragma once

#include "pm/http_client.hpp"
#include "pm/types.hpp"

#include <map>
#include <optional>
#include <string>

namespace pm {

class ClobClient {
 public:
  explicit ClobClient(HttpClient&& http);

  std::optional<TokenBook> fetch_book(const std::string& token_id) const;
  std::optional<std::string> post_order(
      const std::string& body,
      const std::map<std::string, std::string>& auth_headers) const;

 private:
  HttpClient http_;
};

}  // namespace pm
