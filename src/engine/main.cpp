#include <charconv>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <pthread.h>
#include <system_error>
#include <thread>

#include "common/logging/logger.hpp"
#include "engine/v3/engine.hpp"

namespace {
uint16_t env_u16(const char *name, uint16_t fallback) {
  if (const char *value = std::getenv(name);
      value != nullptr && value[0] != '\0') {
    unsigned int parsed = 0;
    const char *const end = value + std::strlen(value);
    const auto [ptr, ec] = std::from_chars(value, end, parsed);
    if (ec == std::errc{} && ptr == end &&
        parsed <= std::numeric_limits<uint16_t>::max()) {
      return static_cast<uint16_t>(parsed);
    }
    LOG_ERROR("Invalid value for ", name, ": ", value, "; using default ",
              fallback);
  }
  return fallback;
}
} // namespace

int main(int argc, char **argv) {
  auto &logger = log::Logger::instance();
  if (!logger.set_log_file("engine_app.log")) {
    LOG_ERROR("Failed to open engine_app.log for writing");
    return 1;
  }
  logger.set_level(Level::DEBUG);
  LOG_INFO("<======== Engine ========>");

  // Block SIGINT/SIGTERM in this thread and wait on them explicitly.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);

  if (!engine::v3::DpdkRxSource::init_eal(argc, argv)) {
    LOG_ERROR("Failed to initialize DPDK for engine v3");
    return 1;
  }

  engine::v3::Engine engine;
  engine::v3::DpdkRxSource::Config cfg{};
  cfg.port_id = env_u16("MD_ENGINE_DPDK_PORT_ID", 0);
  cfg.queue_id = env_u16("MD_ENGINE_DPDK_QUEUE_ID", 0);

  auto rx_source = engine::v3::DpdkRxSource::create(cfg);
  if (!engine.attach(*rx_source)) {
    LOG_ERROR("Failed to attach v3 DPDK RX source");
    return 1;
  }

  if (!rx_source->open()) {
    LOG_ERROR("Failed to start engine DPDK receiver on port ", cfg.port_id,
              " queue ", cfg.queue_id);
    return 1;
  }

  std::thread engine_thread;
  try {
    engine_thread = std::thread([&engine] {
      if (!engine.run()) {
        LOG_ERROR("Engine v3 run loop exited with failure");
      }
    });
  } catch (const std::system_error &e) {
    rx_source->close();
    LOG_ERROR("Failed to start engine v3 thread: ", e.what());
    return 1;
  }

  LOG_INFO("Engine DPDK receiver is running on port ", cfg.port_id, " queue ",
           cfg.queue_id);

  // Wait until a termination signal arrives.
  int sig = 0;
  sigwait(&set, &sig);

  engine.request_stop();
  if (engine_thread.joinable()) {
    engine_thread.join();
  }
  LOG_INFO("Engine stopped!!!");
  return 0;
}
