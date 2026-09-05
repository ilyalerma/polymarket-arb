#include "pm/spsc_ring.hpp"

#include <cassert>
#include <thread>

int main() {
  pm::SpscRing<int, 4> ring;

  int value = 0;
  assert(!ring.try_pop(value));
  assert(ring.size_approx() == 0);

  assert(ring.try_push(1));
  assert(ring.try_push(2));
  assert(ring.size_approx() == 2);

  assert(ring.try_pop(value));
  assert(value == 1);
  assert(ring.try_pop(value));
  assert(value == 2);
  assert(!ring.try_pop(value));

  int produced = 0;
  int consumed = 0;
  std::thread producer([&] {
    for (int i = 0; i < 1000; ++i) {
      while (!ring.try_push(i)) {
      }
      ++produced;
    }
  });
  std::thread consumer([&] {
    int item = 0;
    while (consumed < 1000) {
      if (ring.try_pop(item)) {
        ++consumed;
      }
    }
  });

  producer.join();
  consumer.join();
  assert(produced == 1000);
  assert(consumed == 1000);

  return 0;
}
