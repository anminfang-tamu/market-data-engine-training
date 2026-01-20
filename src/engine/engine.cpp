#include "engine.hpp"
#include "common/logging/logger.hpp"
#include "engine/normalize/normalize.hpp"
#include "protocol/md_message.hpp"

namespace engine {

Engine::~Engine() { stop(); }

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
    std::unique_lock<std::mutex> lock(mtx_);
    while (running_.load(std::memory_order_relaxed)) {
      cv_.wait_for(lock, std::chrono::seconds(5));
      if (running_.load(std::memory_order_relaxed) == false)
        break;
      auto snap = m_.snapshot();
      LOG_INFO("Metrics ", m_.to_string(snap));
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
