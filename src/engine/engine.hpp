#pragma once
#include <atomic>
#include <thread>

#include "common/metrics/metrics.hpp"
#include "common/net/udp_server.hpp"
#include "containers/frame_pool.hpp"

namespace engine {
class Engine {
public:
  Engine() = default;
  ~Engine();

  bool run();
  bool stop();

  void process_loop();

private:
  std::atomic<bool> running_{false};

  uint64_t expected_seq_num_{0};
  bool seq_initialized_{false};

  metrics::Metrics m_;
  std::thread consumer_;
  std::thread reporter_;

  net::udp::ServerHandle handle_;

  containers::FramePool<4096> pool_;

  int io_cpu_{1};
  int process_cpu_{2};
  int metrics_cpu_{3};
};
} // namespace engine
