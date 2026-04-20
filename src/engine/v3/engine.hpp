#pragma once

#include <csignal>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace engine::v3 {

struct PacketView {
  const std::byte *data{nullptr};
  std::size_t len{0};
  void *handle{nullptr};
};

class DpdkRxSource {
public:
  struct Config {
    uint16_t port_id{0};
    uint16_t queue_id{0};
    uint16_t rx_desc{1024};
    uint32_t mbuf_pool_size{8192};
    uint32_t mbuf_cache_size{256};
    int socket_id{-1};
    bool promiscuous{true};
    std::string mempool_name{"md_engine_rx"};
  };

  static bool init_eal(int argc, char **argv);
  static std::unique_ptr<DpdkRxSource> create();
  static std::unique_ptr<DpdkRxSource> create(Config cfg);

  virtual ~DpdkRxSource() = default;

  virtual bool open() = 0;
  virtual void close() = 0;
  virtual std::size_t poll(PacketView *packets, std::size_t capacity) = 0;
  virtual void release(PacketView *packets, std::size_t count) = 0;
};

struct EngineConfig {
  std::size_t burst_size{32};
  int hot_numa_node{-1};
  int hot_cpu{-1};
  uint16_t udp_dst_port{8888};
  bool log_each_packet{false};
  volatile std::sig_atomic_t *stop_signal_flag{nullptr};
};

class Engine {
public:
  explicit Engine(EngineConfig cfg = {}) : cfg_(cfg) {}
  ~Engine() = default;

  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;

  // not owning, Rx must outlive Engine
  bool attach(DpdkRxSource &rs);

  // Runs the receive -> decode -> sequence-check loop on the caller thread.
  bool run();
  void request_stop();

private:
  void process_one(const PacketView &packet);

  std::atomic<bool> stop_requested_{false};
  EngineConfig cfg_{};
  DpdkRxSource *rx_source_{nullptr};

  uint64_t expected_seq_num_{0};
  bool seq_initialized_{false};
};

} // namespace engine::v3
