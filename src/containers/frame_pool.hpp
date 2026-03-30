#pragma once
#include <array>

#include "containers/ring_buffer.hpp"
#include "protocol/encode.hpp"

namespace containers {
using Frame = std::array<uint8_t, protocol::kWireSize>;

template <size_t Capacity> struct FramePool {
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

  std::array<Frame, Capacity> slots{};
  RingBuffer<size_t, Capacity> free;
  RingBuffer<size_t, Capacity> ready;

  void init() {
    free.reset();
    ready.reset();
    for (size_t i = 0; i < Capacity; ++i) {
      free.push(i);
    }
  }
};
} // namespace containers
