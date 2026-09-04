#pragma once

#include <map>
#include <optional>
#include <string>

namespace pm {

class HttpClient {
 public:
  explicit HttpClient(std::string base_url);
  ~HttpClient();

  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;
  HttpClient(HttpClient&&) noexcept = default;
  HttpClient& operator=(HttpClient&&) noexcept = default;

  std::optional<std::string> get(
      const std::string& path,
      const std::map<std::string, std::string>& query = {}) const;

  std::optional<std::string> post(
      const std::string& path,
      const std::string& body,
      const std::map<std::string, std::string>& headers = {}) const;

 private:
  std::string base_url_;
};

}  // namespace pm
