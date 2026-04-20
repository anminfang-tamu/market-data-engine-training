
#pragma once

#include "common/net/v2/udp_sender.hpp"
#include "protocol/md_message.hpp"

#include <atomic>

namespace generator::v2 {

class Generator {
public:
  Generator() = default;
  ~Generator();

  bool open(const net::udp::v2::SenderConfig &cfg);
  bool send_one(const protocol::MarketDataMsg &msg);
  int send_batch(const protocol::MarketDataMsg *msgs, size_t count);
  bool run(int count, int rate, int seed, int gap_mod = 0, int gap_span = 1,
           int burst_size = 1);

private:
  std::atomic<bool> running_{false};
  net::udp::v2::SenderConfig cfg_;
  net::udp::v2::Sender sender_;

  uint32_t symbol_count_{1};
  uint32_t rng_state_{1}; // random number generator(RNG)
  uint64_t seq_{0};

  uint32_t next_rand();
  protocol::MarketDataMsg make_message();
  bool send_bytes(const void *data, size_t len);
};

} // namespace generator::v2
