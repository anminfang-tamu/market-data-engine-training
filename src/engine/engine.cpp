#include "engine.hpp"
#include "common/logging/logger.hpp"
#include "common/thread/affinity.hpp"
#include "engine/normalize/normalize.hpp"
#include "protocol/decode.hpp"
#include "protocol/md_message.hpp"

#include <cerrno>
#include <chrono>
#include <immintrin.h>
#include <sys/timerfd.h>
#include <thread>
#include <unistd.h>

namespace engine {

namespace {
inline void spin_pause() { _mm_pause(); }
} // namespace

Engine::~Engine() { stop(); }

bool Engine::run() {
  int port = 8888;
  const char *bind_ip = "127.0.0.1";

  running_.store(true, std::memory_order_relaxed);

  pool_.init();

  if (!net::udp::start_server(handle_, pool_, m_, port, io_cpu_, bind_ip)) {
    running_.store(false, std::memory_order_relaxed);
    return false;
  }

  consumer_ = std::thread([this] {
    common::thread::pin_current_thread(process_cpu_);
    process_loop();
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

bool Engine::stop() {
  running_.store(false, std::memory_order_relaxed);
  net::udp::stop_server(handle_);

  if (consumer_.joinable()) {
    consumer_.join();
  }
  if (reporter_.joinable()) {
    reporter_.join();
  }
  return true;
}

void Engine::process_loop() {
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
} // namespace engine
