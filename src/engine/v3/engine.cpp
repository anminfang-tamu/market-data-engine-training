#include "engine/v3/engine.hpp"

#include "common/logging/logger.hpp"
#include "protocol/decode.hpp"
#include "protocol/md_message.hpp"

#include <netinet/in.h>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_udp.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace engine::v3 {

namespace {

bool extract_udp_payload(const PacketView &packet, uint16_t expected_dst_port,
                         const uint8_t *&payload, std::size_t &payload_len) {
  payload = nullptr;
  payload_len = 0;

  constexpr std::size_t kEtherHdrLen = sizeof(rte_ether_hdr);
  constexpr std::size_t kUdpHdrLen = sizeof(rte_udp_hdr);

  if (packet.data == nullptr || packet.len < kEtherHdrLen) {
    return false;
  }

  const auto *frame = reinterpret_cast<const uint8_t *>(packet.data);
  const auto *ether = reinterpret_cast<const rte_ether_hdr *>(frame);
  if (rte_be_to_cpu_16(ether->ether_type) != RTE_ETHER_TYPE_IPV4) {
    return false;
  }

  if (packet.len < kEtherHdrLen + sizeof(rte_ipv4_hdr)) {
    return false;
  }

  const auto *ipv4 =
      reinterpret_cast<const rte_ipv4_hdr *>(frame + kEtherHdrLen);
  const uint8_t version_ihl = ipv4->version_ihl;
  const uint8_t version = static_cast<uint8_t>(version_ihl >> 4);
  const std::size_t ip_hdr_len =
      static_cast<std::size_t>(version_ihl & 0x0fU) * 4U;
  if (version != 4 || ip_hdr_len < sizeof(rte_ipv4_hdr)) {
    return false;
  }

  const std::size_t ip_offset = kEtherHdrLen;
  if (packet.len < ip_offset + ip_hdr_len + kUdpHdrLen) {
    return false;
  }

  if (ipv4->next_proto_id != IPPROTO_UDP) {
    return false;
  }

  const std::size_t ip_total_len = rte_be_to_cpu_16(ipv4->total_length);
  if (ip_total_len < ip_hdr_len + kUdpHdrLen) {
    return false;
  }

  const std::size_t udp_offset = ip_offset + ip_hdr_len;
  if (packet.len < ip_offset + ip_total_len) {
    return false;
  }

  const auto *udp = reinterpret_cast<const rte_udp_hdr *>(frame + udp_offset);
  const uint16_t dst_port = rte_be_to_cpu_16(udp->dst_port);
  if (expected_dst_port != 0 && dst_port != expected_dst_port) {
    return false;
  }

  const std::size_t udp_len = rte_be_to_cpu_16(udp->dgram_len);
  if (udp_len < kUdpHdrLen) {
    return false;
  }

  payload = frame + udp_offset + kUdpHdrLen;
  payload_len = udp_len - kUdpHdrLen;
  return packet.len >= (udp_offset + kUdpHdrLen + payload_len);
}

} // namespace

class DpdkRxSourceImpl : public DpdkRxSource {
public:
  explicit DpdkRxSourceImpl(DpdkRxSource::Config cfg) : cfg_(std::move(cfg)) {}

  bool open() override {
    if (is_open_) {
      return true;
    }

    if (cfg_.queue_id != 0) {
      LOG_ERROR("Only queue 0 is supported in the current v3 DPDK source; got ",
                cfg_.queue_id);
      return false;
    }

    const int socket_id = cfg_.socket_id >= 0
                              ? cfg_.socket_id
                              : rte_eth_dev_socket_id(cfg_.port_id);

    if (mbuf_pool_ == nullptr) {
      const std::string mempool_name =
          cfg_.mempool_name + "_" + std::to_string(cfg_.port_id);
      mbuf_pool_ = rte_pktmbuf_pool_create(
          mempool_name.c_str(), cfg_.mbuf_pool_size, cfg_.mbuf_cache_size, 0,
          RTE_MBUF_DEFAULT_BUF_SIZE, socket_id);
      if (mbuf_pool_ == nullptr) {
        LOG_ERROR("Failed to create DPDK mbuf pool for port ", cfg_.port_id,
                  ": ", rte_strerror(rte_errno));
        return false;
      }
    }

    rte_eth_conf port_conf{};
    const int configure_ret =
        rte_eth_dev_configure(cfg_.port_id, 1, 0, &port_conf);
    if (configure_ret < 0) {
      LOG_ERROR("Failed to configure DPDK port ", cfg_.port_id, ": ",
                rte_strerror(-configure_ret));
      reset_mbuf_pool();
      return false;
    }

    const int queue_ret =
        rte_eth_rx_queue_setup(cfg_.port_id, cfg_.queue_id, cfg_.rx_desc,
                               socket_id, nullptr, mbuf_pool_);
    if (queue_ret < 0) {
      LOG_ERROR("Failed to configure RX queue ", cfg_.queue_id, " on port ",
                cfg_.port_id, ": ", rte_strerror(-queue_ret));
      rte_eth_dev_close(cfg_.port_id);
      reset_mbuf_pool();
      return false;
    }

    int ret = rte_eth_dev_start(cfg_.port_id);
    if (ret < 0) {
      LOG_ERROR("Failed to start DPDK port ", cfg_.port_id, ": ",
                rte_strerror(-ret));
      rte_eth_dev_close(cfg_.port_id);
      reset_mbuf_pool();
      return false;
    }

    if (cfg_.promiscuous) {
      ret = rte_eth_promiscuous_enable(cfg_.port_id);
      if (ret < 0) {
        LOG_WARN("Failed to enable promiscuous mode on port ", cfg_.port_id,
                 ": ", rte_strerror(-ret));
      }
    }

    is_open_ = true;
    return true;
  }

  void close() override {
    if (!is_open_ && mbuf_pool_ == nullptr) {
      return;
    }

    if (is_open_) {
      rte_eth_dev_stop(cfg_.port_id);
      rte_eth_dev_close(cfg_.port_id);
      is_open_ = false;
    }

    reset_mbuf_pool();
  }

  std::size_t poll(PacketView *packets, std::size_t capacity) override {
    static constexpr uint16_t kMaxBurst = 256;
    rte_mbuf *mbufs[kMaxBurst];
    const uint16_t burst =
        static_cast<uint16_t>(capacity < kMaxBurst ? capacity : kMaxBurst);
    const uint16_t n =
        rte_eth_rx_burst(cfg_.port_id, cfg_.queue_id, mbufs, burst);

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
  void reset_mbuf_pool() {
    if (mbuf_pool_ != nullptr) {
      rte_mempool_free(mbuf_pool_);
      mbuf_pool_ = nullptr;
    }
  }

  DpdkRxSource::Config cfg_;
  rte_mempool *mbuf_pool_{nullptr};
  bool is_open_{false};
};

bool DpdkRxSource::init_eal(int argc, char **argv) {
  const int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    LOG_ERROR("Failed to initialize DPDK EAL: ", rte_strerror(rte_errno));
    return false;
  }

  const uint16_t port_count = rte_eth_dev_count_avail();
  if (port_count == 0) {
    LOG_ERROR("DPDK EAL initialized but no Ethernet devices are available");
    return false;
  }

  LOG_INFO("DPDK EAL initialized; available Ethernet ports=", port_count);
  return true;
}

std::unique_ptr<DpdkRxSource> DpdkRxSource::create() {
  return create(Config{});
}

std::unique_ptr<DpdkRxSource> DpdkRxSource::create(Config cfg) {
  return std::make_unique<DpdkRxSourceImpl>(std::move(cfg));
}

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

  while (!stop_requested_.load(std::memory_order_acquire) &&
         (cfg_.stop_signal_flag == nullptr || *cfg_.stop_signal_flag == 0)) {
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

  const uint8_t *payload = nullptr;
  std::size_t payload_len = 0;
  if (!extract_udp_payload(packet, cfg_.udp_dst_port, payload, payload_len)) {
    return;
  }

  protocol::MarketDataMsg msg{};
  if (!protocol::decode(payload, payload_len, msg)) {
    LOG_WARN("Decode failed: udp_payload_len=", payload_len);
    return;
  }

  if (cfg_.log_each_packet) {
    LOG_INFO("Parsed packet: frame_len=", packet.len,
             " udp_payload_len=", payload_len,
             " seq=", msg.seq_num,
             " symbol=", msg.symbol_id,
             " ts=", msg.exchange_ts,
             " bid=", msg.bid_price,
             " ask=", msg.ask_price,
             " bid_size=", msg.bid_size,
             " ask_size=", msg.ask_size);
  }

  if (!seq_initialized_) {
    expected_seq_num_ = msg.seq_num + 1;
    seq_initialized_ = true;
    return;
  }

  if (msg.seq_num == expected_seq_num_) {
    ++expected_seq_num_;
  } else if (msg.seq_num > expected_seq_num_) {
    LOG_WARN("Sequence gap: expected=", expected_seq_num_, " got=", msg.seq_num,
             " gap=", msg.seq_num - expected_seq_num_);
    expected_seq_num_ = msg.seq_num + 1;
  } else {
    LOG_WARN("Out-of-order: expected=", expected_seq_num_,
             " got=", msg.seq_num);
  }
}

} // namespace engine::v3
