#include "common/logging/logger.hpp"
#include "generator/v2/generator.hpp"

int main() {
  auto &logger = log::Logger::instance();
  if (!logger.set_log_file("generator_app.log")) {
    LOG_ERROR("Failed to open generator_app.log for writing");
    return 1;
  }
  logger.set_level(Level::DEBUG);
  LOG_INFO("<======== Generator ========>");

  generator::v2::Generator gen;
  net::udp::v2::SenderConfig cfg;
  cfg.local_ip = "0.0.0.0";
  cfg.local_port = 0;
  cfg.remote_ip = "127.0.0.1";
  cfg.remote_port = 8888;
  cfg.sndbuf_bytes = 4 * 1024 * 1024;
  cfg.connect_socket = true;
  // constexpr const char *host = "127.0.0.1";
  // constexpr int port = 8888;
  bool connected = gen.open(cfg);

  if (connected) {
    LOG_INFO("UDP destination configured: ", cfg.remote_ip, ":",
             cfg.remote_port);
  } else {
    LOG_ERROR("Failed to configure UDP destination ", cfg.remote_ip, ":",
              cfg.remote_port);
    return 1;
  }

  // 10-minute soak
  const int rate = 50'000;
  const int duration_s = 600; // 10 minutes
  const int count = rate * duration_s;
  const int seed = 42;
  const int gap_mod = 1000; // inject a gap when rand % gap_mod < gap_span
  const int gap_span = 1;   // skip this many seq numbers when triggered

  if (!gen.run(count, rate, seed, gap_mod, gap_span)) {
    LOG_ERROR("Generator run failed");
    return 1;
  }

  // burst + normal
  // const int burst_count = 100'000;
  // const int burst_rate = 0;
  // const int burst_seed = 43;

  // const int normal_rate = 50'000;
  // const int normal_duration = 600; // seconds
  // const int normal_count = normal_rate * normal_duration;
  // const int normal_seed = 44;

  // gen.run(burst_count, burst_rate, burst_seed);
  // gen.run(normal_count, normal_rate, normal_seed);

  LOG_INFO("<========== END ==========>");
  return 0;
}
