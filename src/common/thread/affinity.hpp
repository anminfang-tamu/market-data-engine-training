#pragma once

#include <pthread.h>
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

} // namespace common::thread
