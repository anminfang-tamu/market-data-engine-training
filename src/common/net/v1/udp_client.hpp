#pragma once

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol/encode.hpp"

namespace net::udp {

struct Client {
  int fd{-1};
  bool connected{false};
};

inline bool open_client(Client &client, const char *remote_ip, uint16_t remote_port,
                        const char *local_bind_ip = nullptr,
                        uint16_t local_bind_port = 0, int sndbuf_bytes = 0) {
  if (client.fd >= 0) {
    ::close(client.fd);
  }
  client.fd = -1;
  client.connected = false;

  int sock_type = SOCK_DGRAM;
#ifdef SOCK_CLOEXEC
  sock_type |= SOCK_CLOEXEC;
#endif
  int fd = ::socket(AF_INET, sock_type, 0);
  if (fd == -1) {
    std::perror("socket");
    return false;
  }

  if (sndbuf_bytes > 0) {
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_bytes,
                     sizeof(sndbuf_bytes)) == -1) {
      std::perror("setsockopt SO_SNDBUF");
      ::close(fd);
      return false;
    }
  }

  if ((local_bind_ip != nullptr && local_bind_ip[0] != '\0') ||
      local_bind_port != 0) {
    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(local_bind_port);

    if (local_bind_ip == nullptr || local_bind_ip[0] == '\0') {
      local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, local_bind_ip, &local_addr.sin_addr) != 1) {
      std::perror("inet_pton local_bind_ip");
      ::close(fd);
      return false;
    }

    if (::bind(fd, reinterpret_cast<sockaddr *>(&local_addr),
               sizeof(local_addr)) == -1) {
      std::perror("bind local");
      ::close(fd);
      return false;
    }
  }

  sockaddr_in remote_addr{};
  remote_addr.sin_family = AF_INET;
  remote_addr.sin_port = htons(remote_port);
  if (::inet_pton(AF_INET, remote_ip, &remote_addr.sin_addr) != 1) {
    std::perror("inet_pton remote_ip");
    ::close(fd);
    return false;
  }

  if (::connect(fd, reinterpret_cast<sockaddr *>(&remote_addr),
                sizeof(remote_addr)) == -1) {
    std::perror("connect");
    ::close(fd);
    return false;
  }

  client.fd = fd;
  client.connected = true;
  return true;
}

inline void close_client(Client &client) {
  if (client.fd >= 0) {
    ::close(client.fd);
  }
  client.fd = -1;
  client.connected = false;
}

inline bool send_bytes(const Client &client, const void *data, size_t len) {
  if (!client.connected || client.fd < 0 || data == nullptr) {
    return false;
  }
  if (len == 0) {
    return true;
  }

  int send_flags = 0;
#ifdef MSG_NOSIGNAL
  send_flags |= MSG_NOSIGNAL;
#endif

  for (;;) {
    const ssize_t n = ::send(client.fd, data, len, send_flags);
    if (n == static_cast<ssize_t>(len)) {
      return true;
    }
    if (n == -1 && errno == EINTR) {
      continue;
    }
    return false;
  }
}

inline bool send_encoded_frame(
    const Client &client,
    const std::array<uint8_t, protocol::kWireSize> &encoded_frame) {
  return send_bytes(client, encoded_frame.data(), encoded_frame.size());
}

inline bool send_message(const Client &client,
                         const protocol::MarketDataMsg &msg) {
  const auto bytes = protocol::encode(msg);
  return send_encoded_frame(client, bytes);
}

} // namespace net::udp
