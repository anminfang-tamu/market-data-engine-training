#pragma once

#include "common/metrics/metrics.hpp"
#include "common/net/v2/udp_receiver.hpp"
#include "containers/frame_pool.hpp"
#include "containers/numa_frame_pool.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace engine::v2 {

class Engine {

public:
  Engine() = default;
  ~Engine();

  bool receive(const net::udp::v2::ReceiverConfig &cfg);
  void stop();

private:
  static constexpr size_t kPoolCapacity = 16384;
  static constexpr uint64_t kLatencyBucketWidthNs = 1'000;
  static constexpr size_t kLatencyBucketCount = 20'000;

  struct LatencySnapshot {
    uint64_t samples{0};
    uint64_t p50_ns{0};
    uint64_t p99_ns{0};
    uint64_t p999_ns{0};
    uint64_t max_ns{0};
    uint64_t overflow{0};
  };

  void io_loop();
  void process();
  void release_pool();
  void record_latency(uint64_t latency_ns);
  LatencySnapshot snapshot_latency();

  std::atomic<bool> running_{false};
  net::udp::v2::ReceiverConfig cfg_;
  net::udp::v2::Receiver receiver_;

  uint64_t expected_seq_num_{0};
  bool seq_initialized_{false};

  metrics::Metrics m_;
  std::thread io_thread_;
  std::thread consumer_;
  std::thread reporter_;

  containers::FramePool<kPoolCapacity> *pool_{nullptr};
  bool pool_on_numa_{false};
  std::array<std::atomic<uint64_t>, kPoolCapacity> recv_ready_ns_{};
  std::array<std::atomic<uint64_t>, kLatencyBucketCount + 1> latency_buckets_{};
  std::atomic<uint64_t> latency_samples_{0};
  std::atomic<uint64_t> latency_max_ns_{0};

  // NUMA node
  int process_node_{0};
  int io_node_{0};

  // CPU
  int io_cpu_{1};
  int process_cpu_{2};
  int metrics_cpu_{3};
};
} // namespace engine::v2
