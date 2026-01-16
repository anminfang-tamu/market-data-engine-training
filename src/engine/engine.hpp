#pragma once
#include <atomic>
#include <thread>

#include "common/metrics/metrics.hpp"
#include "containers/ring_buffer.hpp"
#include "protocol/md_message.hpp"

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

  int listen_fd_{-1};
  std::atomic<int> client_fd_{-1};

  metrics::Metrics m_;

  std::thread processor_;

  std::thread reporter_;

  containers::RingBuffer<protocol::MarketDataMsg, 4096> queue_;

  void on_message(const void *data, size_t len);
};
} // namespace engine
