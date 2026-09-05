#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace pm {

struct HttpResponse {
  long status_code{0};
  std::string body;
  bool transport_ok{false};

  bool ok() const noexcept { return transport_ok && status_code >= 200 && status_code < 300; }
};

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

  std::optional<std::string> post(
      const std::string& path,
      const char* body,
      std::size_t body_len,
      const std::map<std::string, std::string>& headers = {}) const;

  HttpResponse post_detailed(
      const std::string& path,
      const char* body,
      std::size_t body_len,
      const std::map<std::string, std::string>& headers = {}) const;

 private:
  std::string base_url_;
};

}  // namespace pm
