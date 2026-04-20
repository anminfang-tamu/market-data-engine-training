#include <algorithm>
#include <charconv>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

#include "common/logging/logger.hpp"
#include "engine/v3/engine.hpp"

namespace {
volatile std::sig_atomic_t g_stop_signal = 0;

void handle_stop_signal(int sig) {
  g_stop_signal = sig;
}

bool install_signal_handler(int sig) {
  struct sigaction action {};
  action.sa_handler = handle_stop_signal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  return sigaction(sig, &action, nullptr) == 0;
}

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

bool env_bool(const char *name, bool fallback) {
  if (const char *value = std::getenv(name);
      value != nullptr && value[0] != '\0') {
    std::string parsed(value);
    std::transform(parsed.begin(), parsed.end(), parsed.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (parsed == "1" || parsed == "true" || parsed == "yes" ||
        parsed == "on") {
      return true;
    }
    if (parsed == "0" || parsed == "false" || parsed == "no" ||
        parsed == "off") {
      return false;
    }

    LOG_ERROR("Invalid value for ", name, ": ", value, "; using default ",
              fallback ? "true" : "false");
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

  if (!install_signal_handler(SIGINT) || !install_signal_handler(SIGTERM)) {
    LOG_ERROR("Failed to install signal handlers for engine shutdown");
    return 1;
  }

  if (!engine::v3::DpdkRxSource::init_eal(argc, argv)) {
    LOG_ERROR("Failed to initialize DPDK for engine v3");
    return 1;
  }

  engine::v3::EngineConfig engine_cfg{};
  engine_cfg.udp_dst_port = env_u16("MD_ENGINE_UDP_PORT", 8888);
  engine_cfg.log_each_packet =
      env_bool("MD_ENGINE_LOG_EACH_PACKET", false);
  engine_cfg.stop_signal_flag = &g_stop_signal;

  engine::v3::Engine engine(engine_cfg);
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

  LOG_INFO("Engine DPDK receiver is running on port ", cfg.port_id, " queue ",
           cfg.queue_id, " udp_dst_port ", engine_cfg.udp_dst_port,
           " log_each_packet ",
           engine_cfg.log_each_packet ? "true" : "false");

  if (!engine.run()) {
    LOG_ERROR("Engine v3 run loop exited with failure");
    return 1;
  }

  if (g_stop_signal != 0) {
    LOG_INFO("Termination signal received: ", g_stop_signal);
  }
  LOG_INFO("Engine stopped!!!");
  return 0;
}
