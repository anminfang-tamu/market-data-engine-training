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

struct SenderConfig {
  const char *local_ip{"0.0.0.0"};
  uint16_t local_port{0};

  const char *remote_ip{"127.0.0.1"};
  uint16_t remote_port{0};

  int sndbuf_bytes{0};
  bool connect_socket{true};
};

struct TxFrame {
  const void *data{nullptr};
  size_t len{0};
};

struct SenderStats {
  uint64_t sent_packets{0};
  uint64_t sent_bytes{0};
  uint64_t sent_errors{0};
  uint64_t eagain{0};
};

/*
    this is for generator to send binary data
*/
class Sender {
public:
  bool open(const SenderConfig &cfg);
  void close();

  bool send_one(const void *data, size_t len);
  int send_batch(const TxFrame *frames, size_t count);

  int fd() const { return fd_; }
  bool running() const { return fd_ >= 0; }
  const SenderStats &stats() const { return stats_; }

private:
  int fd_{-1};
  bool connected_{false};
  sockaddr_in remote_{};
  SenderStats stats_{};
};

} // namespace net::udp::v2
