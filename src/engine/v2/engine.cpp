#include "engine/v2/engine.hpp"

#include "common/logging/logger.hpp"
#include "common/thread/affinity.hpp"
#include "protocol/decode.hpp"
#include "protocol/md_message.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <immintrin.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>

namespace engine::v2 {

namespace {
inline void spin_pause() { _mm_pause(); }
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
    close(tfd);
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

void Engine::io_loop() {
  constexpr size_t kBatch = 32;
  std::array<containers::Frame, kBatch> buffers{};
  std::array<net::udp::v2::RxFrame, kBatch> frames{};

  for (size_t i = 0; i < kBatch; ++i) {
    frames[i].data = buffers[i].data();
    frames[i].capacity = buffers[i].size();
  }

  while (running_.load(std::memory_order_relaxed)) {
    const int n = receiver_.epoll_receive(frames.data(), frames.size(), 500);
    if (n > 0) {
      for (int i = 0; i < n; ++i) {
        if (frames[i].truncated || frames[i].len != protocol::kWireSize) {
          m_.inc_drops();
          continue;
        }

        size_t idx = 0;
        if (!pool_.free.pop(idx)) {
          m_.inc_drops();
          continue;
        }

        std::memcpy(pool_.slots[idx].data(), buffers[i].data(),
                    protocol::kWireSize);
        if (!pool_.ready.push(idx)) {
          m_.inc_drops();
          pool_.free.push(idx);
        }
      }
      continue;
    }

    if (n == 0) {
      continue;
    }

    if (running_.load(std::memory_order_relaxed)) {
      LOG_ERROR("Receiver epoll_receive failed");
      running_.store(false, std::memory_order_relaxed);
    }
    break;
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
