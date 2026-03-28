#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>

namespace net::udp::v2 {

struct ReceiverConfig {
  const char *local_ip{"0.0.0.0"};
  uint16_t local_port{0};

  const char *remote_ip{"127.0.0.1"};
  uint16_t remote_port{0};

  int rcvbuf_bytes{0};
  bool connect_socket{true};
};

struct RxFrame {
  void *data{nullptr};
  size_t capacity{0};

  size_t len{0};
  sockaddr_in peer{};
  bool truncated{false};
};

struct ReceiverStats {
  uint64_t received_packets{0};
  uint64_t received_bytes{0};
  uint64_t received_errors{0};
  uint64_t eagain{0};
};

/*
    this is for engine to receive binary data
*/
class Receiver {
public:
  Receiver() = default;
  ~Receiver() { close(); }

  Receiver(const Receiver &) = delete;
  Receiver &operator=(const Receiver &) = delete;

  Receiver(Receiver &&other) noexcept;
  Receiver &operator=(Receiver &&other) noexcept;

  bool open(const ReceiverConfig &cfg);
  void close();

  bool receive_one(RxFrame &frame);
  int receive_batch(RxFrame *frames, size_t count);
  int epoll_receive(RxFrame *frames, size_t count, int timeout_ms = -1);

  int fd() const { return fd_; }
  const ReceiverStats &stats() const { return stats_; }

private:
  bool ensure_epoll();

  int fd_{-1};
  int epfd_{-1};
  bool connected_{false};
  sockaddr_in remote_{};
  ReceiverStats stats_{};
};

} // namespace net::udp::v2
