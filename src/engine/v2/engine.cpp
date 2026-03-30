#include "engine/v2/engine.hpp"

#include "common/logging/logger.hpp"
#include "common/thread/affinity.hpp"
#include "protocol/decode.hpp"
#include "protocol/md_message.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <thread>
#include <unistd.h>

#include <iostream>

#if defined(__linux__)
#include <sys/timerfd.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace engine::v2 {

namespace {
inline void spin_pause() {
#if defined(__x86_64__) || defined(__i386__)
  _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ __volatile__("yield");
#else
  std::this_thread::yield();
#endif
}
} // namespace

Engine::~Engine() { stop(); }

bool Engine::receive(const net::udp::v2::ReceiverConfig &cfg) {
  if (running_.load(std::memory_order_relaxed)) {
    return false;
  }

  cfg_ = cfg;
  pool_.init();
  expected_seq_num_ = 0;
  seq_initialized_ = false;

  if (!receiver_.open(cfg_)) {
    return false;
  }

  running_.store(true, std::memory_order_relaxed);

  io_thread_ = std::thread([this] {
    common::thread::pin_current_thread(io_cpu_);
    io_loop();
  });

  consumer_ = std::thread([this] {
    common::thread::pin_current_thread(process_cpu_);
    process();
  });

  reporter_ = std::thread([this] {
    common::thread::pin_current_thread(metrics_cpu_);
#if defined(__linux__)
    const int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (tfd == -1) {
      while (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!running_.load(std::memory_order_relaxed))
          break;
        auto snap = m_.snapshot();
        LOG_INFO("Metrics ", m_.to_string(snap));
      }
      return;
    }

    itimerspec its{};
    its.it_value.tv_sec = 5;
    its.it_interval.tv_sec = 5;
    timerfd_settime(tfd, 0, &its, nullptr);

    uint64_t expirations = 0;
    while (running_.load(std::memory_order_relaxed)) {
      const ssize_t n = ::read(tfd, &expirations, sizeof(expirations));
      if (n < 0) {
        if (errno == EINTR)
          continue;
        break;
      }
      if (!running_.load(std::memory_order_relaxed))
        break;
      auto snap = m_.snapshot();
      LOG_INFO("Metrics ", m_.to_string(snap));
    }
    ::close(tfd);
#else
    while (running_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      if (!running_.load(std::memory_order_relaxed))
        break;
      auto snap = m_.snapshot();
      LOG_INFO("Metrics ", m_.to_string(snap));
    }
#endif
  });

  return true;
}

void Engine::stop() {
  running_.store(false, std::memory_order_relaxed);
  receiver_.close();
  if (io_thread_.joinable()) {
    io_thread_.join();
  }
  if (consumer_.joinable()) {
    consumer_.join();
  }

  if (reporter_.joinable()) {
    reporter_.join();
  }
}

// cycle: free -> io_loop -> ready -> process -> free
void Engine::io_loop() {
  constexpr size_t kBatch = 32;
  std::array<size_t, kBatch> indices{};
  std::array<net::udp::v2::RxFrame, kBatch> frames{};

  const auto reserve_frames = [&]() -> size_t {
    size_t reserved = 0;
    for (; reserved < kBatch; ++reserved) {
      if (!pool_.free.pop(indices[reserved])) {
        break;
      }

      // pointing frame to actual slot
      auto &frame = frames[reserved];
      frame.data = pool_.slots[indices[reserved]].data();
      frame.capacity = pool_.slots[indices[reserved]].size();
    }
    return reserved;
  };

  const auto release_reserved = [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      pool_.free.push(indices[i]);
    }
  };

  const auto publish_batch = [&](int received, size_t reserved) {
    for (int i = 0; i < received; ++i) {
      if (frames[i].truncated || frames[i].len != protocol::kWireSize) {
        m_.inc_drops();
        pool_.free.push(indices[i]);
        continue;
      }

      if (!pool_.ready.push(indices[i])) {
        m_.inc_drops();
        pool_.free.push(indices[i]);
      }
    }

    release_reserved(static_cast<size_t>(received), reserved);
  };

  while (running_.load(std::memory_order_relaxed)) {
    size_t reserved = reserve_frames();
    std::cout << "reserved for strace: " << reserved << std::endl;
    if (reserved == 0) {
      spin_pause();
      continue;
    }

    int n = receiver_.epoll_receive(frames.data(), reserved, 500);
    if (n == 0) {
      release_reserved(0, reserved);
      continue;
    }

    if (n < 0) {
      release_reserved(0, reserved);
      if (running_.load(std::memory_order_relaxed)) {
        LOG_ERROR("Receiver epoll_receive failed");
        running_.store(false, std::memory_order_relaxed);
      }
      break;
    }

    bool receive_error = false;
    while (running_.load(std::memory_order_relaxed) && n > 0) {
      publish_batch(n, reserved);

      reserved = reserve_frames();
      if (reserved == 0) {
        break;
      }

      n = receiver_.receive_batch(frames.data(), reserved);
      if (n == 0) {
        release_reserved(0, reserved);
        break;
      }

      if (n < 0) {
        release_reserved(0, reserved);
        receive_error = true;
        break;
      }
    }

    if (receive_error && running_.load(std::memory_order_relaxed)) {
      LOG_ERROR("Receiver receive_batch failed while draining socket");
      running_.store(false, std::memory_order_relaxed);
      break;
    }

    if (n < 0 && running_.load(std::memory_order_relaxed)) {
      LOG_ERROR("Receiver epoll_receive failed");
      running_.store(false, std::memory_order_relaxed);
      break;
    }
  }
}

void Engine::process() {
  protocol::MarketDataMsg msg{};
  while (running_.load(std::memory_order_relaxed) || !pool_.ready.empty()) {
    size_t idx = 0;
    if (pool_.ready.pop(idx)) {
      if (protocol::decode(pool_.slots[idx].data(), protocol::kWireSize, msg)) {
        m_.inc_received();

        if (!seq_initialized_) {
          expected_seq_num_ = msg.seq_num + 1;
          seq_initialized_ = true;
          m_.inc_processed();
        } else if (msg.seq_num == expected_seq_num_) {
          ++expected_seq_num_;
          m_.inc_processed();
        } else if (msg.seq_num > expected_seq_num_) {
          const uint64_t gap = msg.seq_num - expected_seq_num_;
          m_.inc_seq_num_gaps(gap);
          expected_seq_num_ = msg.seq_num + 1;
          m_.inc_processed();
        } else {
          // old/duplicate/out-of-order; drop for now
          m_.inc_drops();
        }

      } else {
        m_.inc_decode_error();
      }
      pool_.free.push(idx);
    } else {
      spin_pause();
    }
  }
}

} // namespace engine::v2
