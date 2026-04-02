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
inline bool pin_thread_to_node(int node) {
  if (node < 0) {
    return false;
  }

  if (numa_available() < 0) {
    return false;
  }

  return numa_run_on_node(node) == 0;
}

// 1. pin the current thread to a NUMA node first,
// 2. then narrow it to one CPU
// 3. that must belong to that node.
inline bool pin_current_thread_to_cpu_on_node(int node, int cpu_id) {
  if (node < 0 || cpu_id < 0) {
    return false;
  }

  if (numa_available() < 0) {
    return false;
  }

  // verify cpu belongs to node
  const int cpu_node = numa_node_of_cpu(cpu_id);
  if (cpu_node < 0 || cpu_node != node) {
    return false;
  }

  if (!pin_thread_to_node(node)) {
    return false;
  }

  return pin_current_thread(cpu_id);
}

} // namespace common::thread
