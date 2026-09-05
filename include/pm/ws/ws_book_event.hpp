#pragma once

#include "pm/order_book.hpp"

#include <cstdint>
#include <cstring>
#include <string_view>

namespace pm::ws {

enum class WsEventKind : std::uint8_t {
  PriceChange = 1,
  EventDirty = 2,
  Shutdown = 3,
};

struct WsBookEvent {
  WsEventKind kind{WsEventKind::PriceChange};
  char token_id[96]{};
  char event_key[128]{};
  double price{0.0};
  double size{0.0};
  BookSide side{BookSide::Bid};

  static WsBookEvent shutdown() {
    WsBookEvent event;
    event.kind = WsEventKind::Shutdown;
    return event;
  }

  static WsBookEvent dirty(const char* event_key_in) {
    WsBookEvent event;
    event.kind = WsEventKind::EventDirty;
    copy_string(event.event_key, event_key_in);
    return event;
  }

  static void copy_string(char* dst, const char* src) {
    if (src == nullptr) {
      dst[0] = '\0';
      return;
    }
    std::strncpy(dst, src, 127);
    dst[127] = '\0';
  }

  static void copy_string(char* dst, std::string_view src) {
    const std::size_t len = std::min(src.size(), static_cast<std::size_t>(127));
    std::memcpy(dst, src.data(), len);
    dst[len] = '\0';
  }
};

}  // namespace pm::ws
