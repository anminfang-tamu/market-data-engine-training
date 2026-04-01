#pragma once

#include <new>
#include <numa.h>

#include "containers/v1/frame_pool.hpp"

#include <array>
#include <stdexcept>

namespace containers {

template <size_t Capacity> struct NumaFramePool {
  static FramePool<Capacity> *create(int node) {
    if (numa_available() < 0) {
      throw std::runtime_error("NUMA not available on this machine");
    }

    void *raw = numa_alloc_onnode(sizeof(FramePool<Capacity>), node);
    if (!raw) {
      throw std::bad_alloc();
    }

    auto *pool = new (raw) FramePool<Capacity>();
    pool->init();
    return pool;
  }

  static void destroy(FramePool<Capacity> *pool) {
    if (pool) {
      pool->~FramePool<Capacity>();
      numa_free(pool, sizeof(FramePool<Capacity>));
    }
  }
};
} // namespace containers