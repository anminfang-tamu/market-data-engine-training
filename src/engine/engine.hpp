#pragma once
#include <atomic>
#include <thread>

#include "common/metrics/metrics.hpp"
#include "common/net/tcp_server_nonblock_epoll.hpp"
#include "containers/frame_pool.hpp"
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

  std::thread consumer_;
  std::thread reporter_;

  net::ServerHandle handle_;

  containers::FramePool<4096> pool_;
};
} // namespace engine
