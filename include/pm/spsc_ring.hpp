#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace pm {

// Single-producer single-consumer lock-free ring buffer.
template <typename T, std::size_t Capacity>
class SpscRing {
 public:
  static_assert(Capacity > 1, "SpscRing capacity must be greater than 1");

  bool try_push(const T& value) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = (head + 1) % Capacity;
    if (next == tail_.load(std::memory_order_acquire)) {
      return false;
    }
    slots_[head] = value;
    head_.store(next, std::memory_order_release);
    return true;
  }

  bool try_pop(T& value) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) {
      return false;
    }
    value = slots_[tail];
    tail_.store((tail + 1) % Capacity, std::memory_order_release);
    return true;
  }

  std::size_t size_approx() const {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (head >= tail) {
      return head - tail;
    }
    return Capacity - tail + head;
  }

 private:
  std::array<T, Capacity> slots_{};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
};

}  // namespace pm
