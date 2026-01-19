#include "engine.hpp"
#include "common/logging/logger.hpp"
#include "engine/normalize/normalize.hpp"
#include "protocol/md_message.hpp"

namespace engine {

Engine::~Engine() { stop(); }

/*
bool Engine::run() {
  int port = 8888;
  listen_fd_ = net::make_listen_socket(port);
  if (listen_fd_ < 0) {
    LOG_ERROR("Failed to activate server on port ", port);
    return false;
  }
  LOG_INFO("Listening on port: ", port);

  running_.store(true, std::memory_order_relaxed);

  processor_ = std::thread([this] { process_loop(); });

  reporter_ = std::thread([this] {
    while (running_.load(std::memory_order_relaxed)) {
      auto snap = m_.snapshot();
      LOG_INFO("Metrics ", m_.to_string(snap));
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  });

  while (running_.load(std::memory_order_relaxed)) {
    int cfd = net::accept_one(listen_fd_);
    client_fd_.store(cfd, std::memory_order_relaxed);

    if (!running_.load(std::memory_order_relaxed) || listen_fd_ < 0) {
      break;
    }

    if (cfd < 0) {
      continue;
    }

    // keep blocking sockets but add a timeout so stop() can tear down cleanly
    net::set_recv_timeout(cfd, std::chrono::milliseconds(500));

    net::handle_client(
        cfd, protocol::kWireSize,
        [this](const void *data, size_t len) { on_message(data, len); },
        [this]() { return !running_.load(std::memory_order_relaxed); });

    close(cfd);
    client_fd_.store(-1, std::memory_order_relaxed);
  }

  return true;
}
  */

bool Engine::run() {
  int port = 8888;

  running_.store(true, std::memory_order_relaxed);

  pool_.init();

  if (!net::start_server(handle_, pool_, m_, port)) {
    running_.store(false, std::memory_order_relaxed);
    return false;
  }

  consumer_ = std::thread([this] { process_loop(); });

  reporter_ = std::thread([this] {
    while (running_.load(std::memory_order_relaxed)) {
      auto snap = m_.snapshot();
      LOG_INFO("Metrics ", m_.to_string(snap));
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  });

  return true;
}

bool Engine::stop() {
  running_.store(false, std::memory_order_relaxed);
  net::stop_server(handle_);

  if (consumer_.joinable()) {
    consumer_.join();
  }
  if (reporter_.joinable()) {
    reporter_.join();
  }
  return true;
}

// bool Engine::stop() {
//   running_.store(false, std::memory_order_relaxed);
//   if (listen_fd_ >= 0) {
//     shutdown(listen_fd_, SHUT_RDWR); // immediately shutdown
//     close(listen_fd_);               // might hang out seconds
//     listen_fd_ = -1;
//   }

//   int cfd = client_fd_.exchange(-1, std::memory_order_relaxed);
//   if (cfd >= 0) {
//     shutdown(cfd, SHUT_RDWR);
//     close(cfd);
//   }

//   if (processor_.joinable()) {
//     processor_.join();
//   }
//   if (reporter_.joinable()) {
//     reporter_.join();
//   }
//   return true;
// }

void Engine::process_loop() {
  protocol::MarketDataMsg msg{};
  while (running_.load(std::memory_order_relaxed) || !pool_.ready.empty()) {
    size_t idx = 0;
    if (pool_.ready.pop(idx)) {
      if (protocol::decode(pool_.slots[idx].data(), protocol::kWireSize, msg)) {
        m_.inc_processed();
      } else {
        m_.inc_decode_error();
      }
      pool_.free.push(idx);
    } else {
      // queue is empty but engine still running; yield briefly
      std::this_thread::yield();
    }
  }
}
} // namespace engine
