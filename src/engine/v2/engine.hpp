#pragma once

#include "common/metrics/metrics.hpp"
#include "common/net/v2/udp_receiver.hpp"
#include "containers/frame_pool.hpp"

#include <atomic>
#include <thread>

namespace engine::v2 {

class Engine {

public:
  Engine() = default;
  ~Engine();

  bool receive(const net::udp::v2::ReceiverConfig &cfg);
  void stop();

  void process();

private:
  std::atomic<bool> running_{false};
  net::udp::v2::ReceiverConfig cfg_;
  net::udp::v2::Receiver receiver_;

  uint64_t expected_seq_num_{0};
  bool seq_initialized_{false};

  metrics::Metrics m_;
  std::thread consumer_;
  std::thread reporter_;

  containers::FramePool<4096> pool_;

  int io_cpu_{1};
  int process_cpu_{2};
  int metrics_cpu_{3};
};
} // namespace engine::v2