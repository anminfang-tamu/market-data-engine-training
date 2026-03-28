#include "udp_sender.hpp"

#include <cerrno>
#include <cstdio>

namespace net::udp::v2 {

bool Sender::open(const SenderConfig &cfg) {
  // step 0: close any previous fds
  close();

  if (cfg.remote_ip == nullptr || cfg.remote_ip[0] == '\0' ||
      cfg.remote_port == 0) {
    return false;
  }

  // SOCK_NONBLOCK: if kernel buffer is full, return EAGAIN;
  // or send it immediately
  // SOCK_CLOEXEC: auto close socket if Linux exec..() call
  int sock_type = SOCK_DGRAM;
#ifdef SOCK_NONBLOCK
  sock_type |= SOCK_NONBLOCK;
#endif
#ifdef SOCK_CLOEXEC
  sock_type |= SOCK_CLOEXEC;
#endif

  int sockfd = ::socket(AF_INET, sock_type, 0);
  if (sockfd == -1) {
    std::perror("socket");
    return false;
  }

  if (cfg.sndbuf_bytes > 0) {
    if (::setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &cfg.sndbuf_bytes,
                     sizeof(cfg.sndbuf_bytes)) == -1) {
      std::perror("setsockopt SO_SNDBUF");
      ::close(sockfd);
      return false;
    }
  }

  sockaddr_in local_addr{};
  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons(cfg.local_port);

  if (cfg.local_ip == nullptr || cfg.local_ip[0] == '\0') {
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (::inet_pton(AF_INET, cfg.local_ip, &local_addr.sin_addr) != 1) {
    std::perror("inet_pton local_ip");
    ::close(sockfd);
    return false;
  }

  if (::bind(sockfd, reinterpret_cast<sockaddr *>(&local_addr),
             sizeof(local_addr)) == -1) {
    std::perror("bind");
    ::close(sockfd);
    return false;
  }

  remote_ = {};
  remote_.sin_family = AF_INET;
  remote_.sin_port = htons(cfg.remote_port);
  if (::inet_pton(AF_INET, cfg.remote_ip, &remote_.sin_addr) != 1) {
    std::perror("inet_pton remote_ip");
    ::close(sockfd);
    remote_ = {};
    return false;
  }

  if (cfg.connect_socket) {
    if (::connect(sockfd, reinterpret_cast<const sockaddr *>(&remote_),
                  sizeof(remote_)) == -1) {
      std::perror("connect");
      ::close(sockfd);
      remote_ = {};
      return false;
    }
    connected_ = true;
  } else {
    connected_ = false;
  }

  fd_ = sockfd;

  return true;
}

void Sender::close() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
  fd_ = -1;
  connected_ = false;
  remote_ = {};
}

bool Sender::send_one(const void *data, size_t len) {
  if (fd_ < 0 || data == nullptr || len == 0) {
    ++stats_.sent_errors;
    return false;
  }

  for (;;) {
    const ssize_t n =
        connected_ ? ::send(fd_, data, len, 0)
                   : ::sendto(fd_, data, len, 0,
                              reinterpret_cast<const sockaddr *>(&remote_),
                              sizeof(remote_));
    if (n == static_cast<ssize_t>(len)) {
      ++stats_.sent_packets;
      stats_.sent_bytes += static_cast<uint64_t>(len);
      return true;
    }
    if (n == -1 && errno == EINTR) {
      continue;
    }
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      ++stats_.eagain;
    } else {
      ++stats_.sent_errors;
    }
    return false;
  }
}

int Sender::send_batch(const TxFrame *frames, size_t count) {
  if (fd_ < 0 || (frames == nullptr && count != 0)) {
    ++stats_.sent_errors;
    return -1;
  }

  int sent = 0;

  for (size_t i = 0; i < count; ++i) {
    if (frames[i].data == nullptr || frames[i].len == 0) {
      ++stats_.sent_errors;
      break;
    }

    for (;;) {
      const ssize_t n =
          connected_ ? ::send(fd_, frames[i].data, frames[i].len, 0)
                     : ::sendto(fd_, frames[i].data, frames[i].len, 0,
                                reinterpret_cast<const sockaddr *>(&remote_),
                                sizeof(remote_));
      if (n == static_cast<ssize_t>(frames[i].len)) {
        ++sent;
        ++stats_.sent_packets;
        stats_.sent_bytes += static_cast<uint64_t>(frames[i].len);
        break;
      }
      if (n == -1 && errno == EINTR) {
        continue;
      }
      if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        ++stats_.eagain;
      } else {
        ++stats_.sent_errors;
      }
      return sent;
    }
  }

  return sent;
}

} // namespace net::udp::v2
