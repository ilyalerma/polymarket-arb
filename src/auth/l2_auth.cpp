#include "pm/auth/l2_auth.hpp"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/hmac.h>

#include <chrono>
#include <cstring>

namespace pm::auth {

namespace {

std::string base64_url_encode(const unsigned char* data, std::size_t len) {
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO* mem = BIO_new(BIO_s_mem());
  b64 = BIO_push(b64, mem);
  BIO_write(b64, data, static_cast<int>(len));
  BIO_flush(b64);

  BUF_MEM* buffer = nullptr;
  BIO_get_mem_ptr(b64, &buffer);
  std::string encoded(buffer->data, buffer->length);
  BIO_free_all(b64);

  for (char& ch : encoded) {
    if (ch == '+') {
      ch = '-';
    } else if (ch == '/') {
      ch = '_';
    }
  }
  while (!encoded.empty() && encoded.back() == '=') {
    encoded.pop_back();
  }
  return encoded;
}

std::string current_timestamp_seconds() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
  return std::to_string(seconds);
}

std::map<std::string, std::string> build_l2_headers_impl(
    const std::string& address,
    const std::string& api_key,
    const std::string& api_secret,
    const std::string& api_passphrase,
    const std::string& method,
    const std::string& path,
    const char* body,
    std::size_t body_len) {
  const std::string timestamp = current_timestamp_seconds();

  std::string message;
  message.reserve(timestamp.size() + method.size() + path.size() + body_len);
  message.append(timestamp);
  message.append(method);
  message.append(path);
  if (body_len > 0) {
    message.append(body, body_len);
  }

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;
  HMAC(
      EVP_sha256(),
      api_secret.data(),
      static_cast<int>(api_secret.size()),
      reinterpret_cast<const unsigned char*>(message.data()),
      message.size(),
      digest,
      &digest_len);

  const std::string signature = base64_url_encode(digest, digest_len);

  return {
      {"POLY_ADDRESS", address},
      {"POLY_SIGNATURE", signature},
      {"POLY_TIMESTAMP", timestamp},
      {"POLY_API_KEY", api_key},
      {"POLY_PASSPHRASE", api_passphrase},
  };
}

}  // namespace

std::map<std::string, std::string> build_l2_headers(
    const std::string& address,
    const std::string& api_key,
    const std::string& api_secret,
    const std::string& api_passphrase,
    const std::string& method,
    const std::string& path,
    const std::string& body) {
  return build_l2_headers_impl(
      address,
      api_key,
      api_secret,
      api_passphrase,
      method,
      path,
      body.data(),
      body.size());
}

std::map<std::string, std::string> build_l2_headers(
    const std::string& address,
    const std::string& api_key,
    const std::string& api_secret,
    const std::string& api_passphrase,
    const std::string& method,
    const std::string& path,
    const char* body,
    std::size_t body_len) {
  return build_l2_headers_impl(
      address,
      api_key,
      api_secret,
      api_passphrase,
      method,
      path,
      body,
      body_len);
}

}  // namespace pm::auth
