#include "pm/clob_client.hpp"

#include <nlohmann/json.hpp>

namespace pm {

namespace {

using json = nlohmann::json;

std::vector<BookLevel> parse_levels(const json& levels) {
  std::vector<BookLevel> out;
  if (!levels.is_array()) {
    return out;
  }
  out.reserve(levels.size());
  for (const auto& level : levels) {
    BookLevel parsed;
    parsed.price = std::stod(level.at("price").get<std::string>());
    parsed.size = std::stod(level.at("size").get<std::string>());
    out.push_back(parsed);
  }
  return out;
}

}  // namespace

ClobClient::ClobClient(HttpClient&& http) : http_(std::move(http)) {}

std::optional<TokenBook> ClobClient::fetch_book(const std::string& token_id) const {
  const auto response = http_.get("/book", {{"token_id", token_id}});
  if (!response) {
    return std::nullopt;
  }

  const json body = json::parse(*response);
  TokenBook book;
  book.token_id = token_id;
  book.bids = parse_levels(body.value("bids", json::array()));
  book.asks = parse_levels(body.value("asks", json::array()));
  if (body.contains("tick_size")) {
    book.tick_size = std::stod(body.at("tick_size").get<std::string>());
  }
  if (body.contains("min_order_size")) {
    book.min_order_size = std::stod(body.at("min_order_size").get<std::string>());
  }
  return book;
}

std::optional<std::string> ClobClient::post_order(
    const std::string& body,
    const std::map<std::string, std::string>& auth_headers) const {
  return post_order(body.data(), body.size(), auth_headers);
}

std::optional<std::string> ClobClient::post_order(
    const char* body,
    std::size_t body_len,
    const std::map<std::string, std::string>& auth_headers) const {
  const auto response = post_order_detailed(body, body_len, auth_headers);
  if (!response.ok()) {
    return std::nullopt;
  }
  return response.body;
}

HttpResponse ClobClient::post_order_detailed(
    const char* body,
    std::size_t body_len,
    const std::map<std::string, std::string>& auth_headers) const {
  return http_.post_detailed("/order", body, body_len, auth_headers);
}

}  // namespace pm
