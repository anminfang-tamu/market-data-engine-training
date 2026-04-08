#include "common/logging/logger.hpp"
#include "generator/v2/generator.hpp"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

const char *env_or_default(const char *name, const char *fallback) {
  if (const char *value = std::getenv(name);
      value != nullptr && value[0] != '\0') {
    return value;
  }
  return fallback;
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
    LOG_ERROR("Invalid value for ", name, ": ", value,
              "; using default ", fallback);
  }
  return fallback;
}

int env_int(const char *name, int fallback) {
  if (const char *value = std::getenv(name);
      value != nullptr && value[0] != '\0') {
    int parsed = 0;
    const char *const end = value + std::strlen(value);
    const auto [ptr, ec] = std::from_chars(value, end, parsed);
    if (ec == std::errc{} && ptr == end) {
      return parsed;
    }
    LOG_ERROR("Invalid value for ", name, ": ", value,
              "; using default ", fallback);
  }
  return fallback;
}

} // namespace

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
  cfg.local_ip = env_or_default("MD_GENERATOR_LOCAL_IP", "0.0.0.0");
  cfg.local_port = env_u16("MD_GENERATOR_LOCAL_PORT", 0);
  cfg.remote_ip = env_or_default("MD_GENERATOR_REMOTE_IP", "127.0.0.1");
  cfg.remote_port = env_u16("MD_GENERATOR_REMOTE_PORT", 8888);
  cfg.sndbuf_bytes = 4 * 1024 * 1024;
  cfg.connect_socket = true;
  bool connected = gen.open(cfg);

  if (connected) {
    LOG_INFO("Generator local bind: ", cfg.local_ip, ":", cfg.local_port);
    LOG_INFO("UDP destination configured: ", cfg.remote_ip, ":",
             cfg.remote_port);
  } else {
    LOG_ERROR("Failed to configure UDP destination ", cfg.remote_ip, ":",
              cfg.remote_port);
    return 1;
  }

  // 10-minute soak
  const int rate = env_int("MD_GENERATOR_RATE", 50'000);
  const int duration_s = env_int("MD_GENERATOR_DURATION_S", 600);
  const int count = rate * duration_s;
  const int seed = env_int("MD_GENERATOR_SEED", 42);
  const int gap_mod = env_int("MD_GENERATOR_GAP_MOD", 1000);
  const int gap_span = env_int("MD_GENERATOR_GAP_SPAN", 1);

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
