#include "generator/v2/generator.hpp"
#include "common/logging/logger.hpp"
#include "protocol/encode.hpp"

#include <array>
#include <chrono>
#include <thread>
#include <vector>

namespace generator::v2 {

namespace {

bool log_send_failure(net::udp::v2::Sender &sender, uint64_t seq_num,
                      size_t attempted_packets) {
  const auto &stats = sender.stats();
  LOG_ERROR("Generator send failed at seq=", seq_num,
            " attempted_packets=", attempted_packets,
            " sent_packets=", stats.sent_packets,
            " sent_bytes=", stats.sent_bytes,
            " eagain=", stats.eagain,
            " sent_errors=", stats.sent_errors);
  return false;
}

} // namespace

Generator::~Generator() {
  running_.store(false, std::memory_order_relaxed);
  sender_.close();
}

uint32_t Generator::next_rand() {
  // Classic LCG (Numerical Recipes parameters): state = state * a + c.
  rng_state_ = rng_state_ * 1664525u + 1013904223u;
  return rng_state_;
}

protocol::MarketDataMsg Generator::make_message() {
  const uint32_t r = next_rand();
  uint32_t sym = (symbol_count_ == 0) ? 1u : (r % symbol_count_) + 1u;
  const uint64_t seq = seq_++;
  int64_t ts = static_cast<int64_t>(seq);
  /*
      1. Calculate the range size: (Max - Min) + 1 => (1000 - 10) + 1 = 991
      2. The price range is from 10 to 1000
  */
  int64_t bid = 10 + static_cast<int64_t>(r % 991);
  int64_t ask = bid + 1;
  int32_t size = 1u + (r % 100);
  return {seq, sym, ts, bid, ask, size, size + 1};
}

bool Generator::send_bytes(const void *data, size_t len) {
  return sender_.send_one(data, len);
}

bool Generator::open(const net::udp::v2::SenderConfig &cfg) {
  cfg_ = cfg;
  const bool opened = sender_.open(cfg_);
  running_.store(opened, std::memory_order_relaxed);
  return opened;
}

bool Generator::send_one(const protocol::MarketDataMsg &msg) {
  const auto encoded = protocol::encode(msg);
  return send_bytes(encoded.data(), encoded.size());
}

int Generator::send_batch(const protocol::MarketDataMsg *msgs, size_t count) {
  if (msgs == nullptr) {
    return sender_.send_batch(nullptr, count);
  }

  thread_local std::vector<std::array<uint8_t, protocol::kWireSize>> encoded;
  thread_local std::vector<net::udp::v2::TxFrame> frames;

  if (encoded.size() < count) {
    encoded.resize(count);
    frames.resize(count);
  }

  for (size_t i = 0; i < count; ++i) {
    encoded[i] = protocol::encode(msgs[i]);
    frames[i] = net::udp::v2::TxFrame{.data = encoded[i].data(),
                                      .len = encoded[i].size()};
  }

  return sender_.send_batch(frames.data(), count);
}

bool Generator::run(int count, int rate, int seed, int gap_mod, int gap_span,
                    int burst_size) {
  if (sender_.fd() < 0) {
    return false;
  }

  if (count <= 0) {
    return true;
  }

  running_.store(true, std::memory_order_relaxed);

  if (seed <= 0) {
    seed = 1;
  }

  rng_state_ = static_cast<uint32_t>(seed);
  seq_ = 0;
  const bool inject_gap = gap_mod > 0 && gap_span > 0;
  if (burst_size <= 0) {
    burst_size = 1;
  }

  std::vector<protocol::MarketDataMsg> batch;
  batch.reserve(static_cast<size_t>(burst_size));

  auto send_or_fail = [&](int slots_requested) {
    if (batch.empty()) {
      return true;
    }

    const int sent = send_batch(batch.data(), batch.size());
    if (sent == static_cast<int>(batch.size())) {
      return true;
    }

    const uint64_t failed_seq = batch[static_cast<size_t>(sent)].seq_num;
    return log_send_failure(sender_, failed_seq,
                            static_cast<size_t>(slots_requested));
  };

  int produced = 0;

  // no rate: as fast as possible
  if (rate <= 0) {
    while (produced < count) {
      const int slots =
          std::min(burst_size, count - produced);
      batch.clear();

      for (int i = 0; i < slots; ++i) {
        ++produced;

        if (inject_gap) {
          uint32_t r = next_rand();
          if ((r % gap_mod) < static_cast<uint32_t>(gap_span)) {
            // Skip gap_span sequence numbers, so jump past them before sending
            // next.
            seq_ += static_cast<uint64_t>(gap_span) + 1;
            continue;
          }
        }

        batch.push_back(make_message());
      }

      if (!send_or_fail(slots)) {
        running_.store(false, std::memory_order_relaxed);
        return false;
      }
    }

    running_.store(false, std::memory_order_relaxed);
    return true;
  }

  using clock = std::chrono::steady_clock;
  auto interval = std::chrono::nanoseconds(1000000000LL / rate);
  auto next = clock::now();

  while (produced < count) {
    const int slots =
        std::min(burst_size, count - produced);
    batch.clear();

    for (int i = 0; i < slots; ++i) {
      ++produced;

      if (inject_gap) {
        uint32_t r = next_rand();
        if ((r % gap_mod) < static_cast<uint32_t>(gap_span)) {
          // Skip gap_span sequence numbers; jump ahead so the next sent seq
          // reflects the gap.
          seq_ += static_cast<uint64_t>(gap_span) + 1;
          continue;
        }
      }

      batch.push_back(make_message());
    }

    if (!send_or_fail(slots)) {
      running_.store(false, std::memory_order_relaxed);
      return false;
    }

    next += interval * slots;
    std::this_thread::sleep_until(next);
  }

  running_.store(false, std::memory_order_relaxed);
  return true;
}

} // namespace generator::v2
