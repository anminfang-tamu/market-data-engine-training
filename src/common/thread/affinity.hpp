#pragma once

#include <pthread.h>

#include <numa.h>
#include <sched.h>

namespace common::thread {

// Pin the current thread to a specific CPU; returns true on success.
inline bool pin_current_thread(int cpu_id) {
  cpu_set_t set;
  CPU_ZERO(&set);
  if (cpu_id >= 0) {
    CPU_SET(static_cast<size_t>(cpu_id), &set);
  } else {
    return false;
  }
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

// Restrict the current thread to CPUs that belong to the given NUMA node.
// Returns the node on success, -1 on failure.
inline int pin_thread_to_node(int node) {
  if (node < 0) {
    return -1;
  }

  if (numa_available() < 0) {
    return -1;
  }

  return numa_run_on_node(node) == 0 ? node : -1;
}

} // namespace common::thread
