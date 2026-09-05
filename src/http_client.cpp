#include "pm/http_client.hpp"

#include <curl/curl.h>
#include <sstream>

namespace pm {

namespace {

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

void ensure_curl_initialized() {
  static const bool initialized = [] {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
  }();
  (void)initialized;
}

std::string build_url(
    const std::string& base,
    const std::string& path,
    const std::map<std::string, std::string>& query) {
  std::ostringstream oss;
  oss << base;
  if (!path.empty() && path.front() != '/') {
    oss << '/';
  }
  oss << path;
  if (!query.empty()) {
    oss << '?';
    bool first = true;
    for (const auto& [key, value] : query) {
      if (!first) {
        oss << '&';
      }
      char* escaped_key = curl_easy_escape(nullptr, key.c_str(), static_cast<int>(key.size()));
      char* escaped_value =
          curl_easy_escape(nullptr, value.c_str(), static_cast<int>(value.size()));
      oss << escaped_key << '=' << escaped_value;
      curl_free(escaped_key);
      curl_free(escaped_value);
      first = false;
    }
  }
  return oss.str();
}

}  // namespace

HttpClient::HttpClient(std::string base_url) : base_url_(std::move(base_url)) {
  ensure_curl_initialized();
}

HttpClient::~HttpClient() = default;

std::optional<std::string> HttpClient::get(
    const std::string& path,
    const std::map<std::string, std::string>& query) const {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    return std::nullopt;
  }

  const std::string url = build_url(base_url_, path, query);
  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "polymarket-arb/0.1");

  const CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK || status < 200 || status >= 300) {
    return std::nullopt;
  }
  return response;
}

std::optional<std::string> HttpClient::post(
    const std::string& path,
    const std::string& body,
    const std::map<std::string, std::string>& headers) const {
  return post(path, body.data(), body.size(), headers);
}

std::optional<std::string> HttpClient::post(
    const std::string& path,
    const char* body,
    std::size_t body_len,
    const std::map<std::string, std::string>& headers) const {
  const auto response = post_detailed(path, body, body_len, headers);
  if (!response.ok()) {
    return std::nullopt;
  }
  return response.body;
}

HttpResponse HttpClient::post_detailed(
    const std::string& path,
    const char* body,
    std::size_t body_len,
    const std::map<std::string, std::string>& headers) const {
  HttpResponse result;
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    return result;
  }

  const std::string url = build_url(base_url_, path, {});
  std::string response;
  struct curl_slist* header_list = nullptr;
  header_list = curl_slist_append(header_list, "Content-Type: application/json");
  for (const auto& [key, value] : headers) {
    const std::string header = key + ": " + value;
    header_list = curl_slist_append(header_list, header.c_str());
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_len));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "polymarket-arb/0.1");

  const CURLcode code = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(header_list);
  curl_easy_cleanup(curl);

  result.transport_ok = code == CURLE_OK;
  result.status_code = status;
  result.body = std::move(response);
  return result;
}

}  // namespace pm
