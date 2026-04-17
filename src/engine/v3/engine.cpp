#include "engine/v3/engine.hpp"

#include "common/logging/logger.hpp"
#include "protocol/decode.hpp"
#include "protocol/md_message.hpp"

#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#include <cstring>

namespace engine::v3 {

class DpdkRxSourceImpl : public DpdkRxSource {
public:
  DpdkRxSourceImpl(uint16_t port_id, uint16_t queue_id, rte_mempool *mbuf_pool)
      : port_id_(port_id), queue_id_(queue_id), mbuf_pool_(mbuf_pool) {}

  bool open() override {
    int ret = rte_eth_dev_start(port_id_);
    if (ret < 0) {
      return false;
    }
    rte_eth_promiscuous_enable(port_id_);
    return true;
  }

  void close() override { rte_eth_dev_stop(port_id_); }

  std::size_t poll(PacketView *packets, std::size_t capacity) override {
    static constexpr uint16_t kMaxBurst = 256;
    rte_mbuf *mbufs[kMaxBurst];
    const uint16_t burst =
        static_cast<uint16_t>(capacity < kMaxBurst ? capacity : kMaxBurst);
    const uint16_t n = rte_eth_rx_burst(port_id_, queue_id_, mbufs, burst);

    for (uint16_t i = 0; i < n; ++i) {
      packets[i].data = reinterpret_cast<const std::byte *>(
          rte_pktmbuf_mtod(mbufs[i], const uint8_t *));
      packets[i].len = rte_pktmbuf_data_len(mbufs[i]);
      packets[i].handle = mbufs[i];
    }
    return n;
  }

  void release(PacketView *packets, std::size_t count) override {
    for (std::size_t i = 0; i < count; ++i) {
      rte_pktmbuf_free(static_cast<rte_mbuf *>(packets[i].handle));
    }
  }

private:
  uint16_t port_id_;
  uint16_t queue_id_;
  rte_mempool *mbuf_pool_;
};

bool Engine::attach(DpdkRxSource &rs) {
  if (rx_source_ != nullptr) {
    return false;
  }
  rx_source_ = &rs;
  return true;
}

bool Engine::run() {
  if (rx_source_ == nullptr)
    return false;

  if (!rx_source_->open())
    return false;

  constexpr std::size_t kMaxBurst = 256;
  PacketView packets[kMaxBurst];
  const std::size_t burst =
      cfg_.burst_size < kMaxBurst ? cfg_.burst_size : kMaxBurst;

  while (!stop_requested_.load(std::memory_order_acquire)) {
    const std::size_t n = rx_source_->poll(packets, burst);
    for (std::size_t i = 0; i < n; ++i) {
      process_one(packets[i]);
    }
    if (n > 0) {
      rx_source_->release(packets, n);
    }
  }

  rx_source_->close();
  return true;
}

void Engine::request_stop() {
  stop_requested_.store(true, std::memory_order_release);
}

void Engine::process_one(const PacketView &packet) {
  if (packet.data == nullptr)
    return;

  protocol::MarketDataMsg msg{};
  if (!protocol::decode(reinterpret_cast<const uint8_t *>(packet.data),
                        packet.len, msg)) {
    LOG_WARN("Decode failed: len=", packet.len);
    return;
  }

  if (!seq_initialized_) {
    expected_seq_num_ = msg.seq_num + 1;
    seq_initialized_ = true;
    return;
  }

  if (msg.seq_num == expected_seq_num_) {
    ++expected_seq_num_;
  } else if (msg.seq_num > expected_seq_num_) {
    LOG_WARN("Sequence gap: expected=", expected_seq_num_,
             " got=", msg.seq_num,
             " gap=", msg.seq_num - expected_seq_num_);
    expected_seq_num_ = msg.seq_num + 1;
  } else {
    LOG_WARN("Out-of-order: expected=", expected_seq_num_,
             " got=", msg.seq_num);
  }
}

} // namespace engine::v3
