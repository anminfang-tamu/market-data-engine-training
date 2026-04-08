#include "engine/v2/engine.hpp"

#include "common/logging/logger.hpp"
#include "common/thread/affinity.hpp"
#include "protocol/decode.hpp"
#include "protocol/md_message.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <future>
#include <thread>
#include <unistd.h>

#if defined(__linux__)
#include <sys/timerfd.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace engine::v2 {

namespace {
inline void spin_pause() { _mm_pause(); }

template <size_t Capacity>
containers::FramePool<Capacity> *create_frame_pool(int node, bool &pool_on_numa) {
  if (numa_available() >= 0) {
    pool_on_numa = true;
    return containers::NumaFramePool<Capacity>::create(node);
  }

  auto *pool = new containers::FramePool<Capacity>();
  pool->init();
  pool_on_numa = false;
  return pool;
}

template <size_t Capacity>
void destroy_frame_pool(containers::FramePool<Capacity> *pool,
                        bool pool_on_numa) {
  if (pool == nullptr) {
    return;
  }

  if (pool_on_numa) {
    containers::NumaFramePool<Capacity>::destroy(pool);
    return;
  }

  delete pool;
}
} // namespace

Engine::~Engine() { stop(); }

bool Engine::receive(const net::udp::v2::ReceiverConfig &cfg) {
  if (running_.load(std::memory_order_relaxed)) {
    return false;
  }

  cfg_ = cfg;
  try {
    pool_ = create_frame_pool<16384>(process_node_, pool_on_numa_);
    if (!pool_on_numa_) {
      LOG_INFO("NUMA unavailable; using standard frame pool");
    }
  } catch (const std::exception &e) {
    LOG_ERROR("Failed to allocate engine frame pool on node ", process_node_,
              ": ", e.what());
    pool_ = nullptr;
    pool_on_numa_ = false;
    return false;
  }

  expected_seq_num_ = 0;
  seq_initialized_ = false;

  if (!receiver_.open(cfg_)) {
    release_pool();
    return false;
  }

  std::promise<bool> io_started_promise;
  std::promise<bool> consumer_started_promise;
  auto io_started = io_started_promise.get_future();
  auto consumer_started = consumer_started_promise.get_future();

  running_.store(true, std::memory_order_relaxed);

  try {
    io_thread_ = std::thread([this, started = std::move(io_started_promise)]() mutable {
      if (!common::thread::pin_current_thread_to_cpu_on_node(io_node_, io_cpu_)) {
        LOG_WARN("Continuing without io thread affinity to NUMA node ",
                 io_node_, " CPU ", io_cpu_);
      }
      started.set_value(true);
      io_loop();
    });

    consumer_ = std::thread(
        [this, started = std::move(consumer_started_promise)]() mutable {
          if (!common::thread::pin_current_thread_to_cpu_on_node(process_node_,
                                                                 process_cpu_)) {
            LOG_WARN("Continuing without consumer thread affinity to NUMA node ",
                     process_node_, " CPU ", process_cpu_);
          }
          started.set_value(true);
          process();
        });
  } catch (const std::system_error &e) {
    LOG_ERROR("Failed to create engine worker threads: ", e.what());
    stop();
    return false;
  }

  if (!io_started.get() || !consumer_started.get()) {
    stop();
    return false;
  }

  try {
    reporter_ = std::thread([this] {
      if (!common::thread::pin_current_thread(metrics_cpu_)) {
        LOG_WARN("Continuing without metrics thread affinity to CPU ",
                 metrics_cpu_);
      }

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
    });
  } catch (const std::system_error &e) {
    LOG_ERROR("Failed to create metrics thread: ", e.what());
    stop();
    return false;
  }

  return true;
}

void Engine::release_pool() {
  destroy_frame_pool<16384>(pool_, pool_on_numa_);
  pool_ = nullptr;
  pool_on_numa_ = false;
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

  release_pool();
}

// cycle: free -> io_loop -> ready -> process -> free
void Engine::io_loop() {
  constexpr size_t kBatch = 32;
  std::array<size_t, kBatch> indices{};
  std::array<net::udp::v2::RxFrame, kBatch> frames{};

  const auto reserve_frames = [&]() -> size_t {
    size_t reserved = 0;
    for (; reserved < kBatch; ++reserved) {
      if (!pool_->free.pop(indices[reserved])) {
        break;
      }

      // pointing frame to actual slot
      auto &frame = frames[reserved];
      frame.data = pool_->slots[indices[reserved]].data();
      frame.capacity = pool_->slots[indices[reserved]].size();
    }
    return reserved;
  };

  const auto release_reserved = [&](size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
      pool_->free.push(indices[i]);
    }
  };

  const auto publish_batch = [&](int received, size_t reserved) {
    for (int i = 0; i < received; ++i) {
      if (frames[i].truncated || frames[i].len != protocol::kWireSize) {
        m_.inc_drops();
        pool_->free.push(indices[i]);
        continue;
      }

      if (!pool_->ready.push(indices[i])) {
        m_.inc_drops();
        pool_->free.push(indices[i]);
      }
    }

    release_reserved(static_cast<size_t>(received), reserved);
  };

  while (running_.load(std::memory_order_relaxed)) {
    size_t reserved = reserve_frames();
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
  constexpr size_t kBatch = 32;
  protocol::MarketDataMsg msg{};
  std::array<size_t, kBatch> indices{};

  while (running_.load(std::memory_order_relaxed) || !pool_->ready.empty()) {
    size_t count = 0;
    for (; count < kBatch; ++count) {
      if (!pool_->ready.pop(indices[count])) {
        break;
      }
    }

    if (count != 0) {
      for (size_t i = 0; i < count; ++i) {
        const size_t idx = indices[i];
        if (protocol::decode(pool_->slots[idx].data(), protocol::kWireSize,
                             msg)) {
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
      }

      for (size_t i = 0; i < count; ++i) {
        pool_->free.push(indices[i]);
      }
    } else {
      spin_pause();
    }
  }
}

} // namespace engine::v2
