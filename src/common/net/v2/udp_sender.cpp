#include "udp_sender.hpp"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <thread>
#include <vector>

namespace net::udp::v2 {

namespace {

constexpr int kMaxSendRetries = 200;

bool is_transient_send_error(int err) {
  return err == EAGAIN || err == EWOULDBLOCK || err == ENOBUFS;
}

void backoff_after_send_retry(int retry) {
  if (retry < 8) {
    std::this_thread::yield();
    return;
  }

  std::this_thread::sleep_for(std::chrono::microseconds(50));
}

} // namespace

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

  int retry = 0;
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
    if (n == -1 && is_transient_send_error(errno)) {
      ++stats_.eagain;
      if (retry < kMaxSendRetries) {
        ++retry;
        backoff_after_send_retry(retry);
        continue;
      }
      std::fprintf(stderr, "%s failed after %d retries: %s\n",
                   connected_ ? "send" : "sendto", kMaxSendRetries,
                   std::strerror(errno));
      ++stats_.sent_errors;
      return false;
    }

    if (n >= 0 && n != static_cast<ssize_t>(len)) {
      std::fprintf(stderr,
                   "%s returned short datagram write: expected=%zu actual=%zd\n",
                   connected_ ? "send" : "sendto", len, n);
      ++stats_.sent_errors;
      return false;
    }

    if (n == -1) {
      std::fprintf(stderr, "%s failed: %s\n", connected_ ? "send" : "sendto",
                   std::strerror(errno));
      ++stats_.sent_errors;
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

#if defined(__linux__)
  thread_local std::vector<mmsghdr> msgs;
  thread_local std::vector<iovec> iovecs;

  while (static_cast<size_t>(sent) < count) {
    const size_t remaining = count - static_cast<size_t>(sent);

    if (msgs.size() < remaining) {
      msgs.resize(remaining);
      iovecs.resize(remaining);
    }

    for (size_t i = 0; i < remaining; ++i) {
      const TxFrame &frame = frames[static_cast<size_t>(sent) + i];
      if (frame.data == nullptr || frame.len == 0) {
        ++stats_.sent_errors;
        return sent;
      }

      msgs[i] = {};
      iovecs[i].iov_base = const_cast<void *>(frame.data);
      iovecs[i].iov_len = frame.len;
      msgs[i].msg_hdr.msg_iov = &iovecs[i];
      msgs[i].msg_hdr.msg_iovlen = 1;

      if (!connected_) {
        msgs[i].msg_hdr.msg_name = &remote_;
        msgs[i].msg_hdr.msg_namelen = sizeof(remote_);
      }
    }

    int retry = 0;
    for (;;) {
      const int n = ::sendmmsg(fd_, msgs.data(),
                               static_cast<unsigned int>(remaining), 0);
      if (n > 0) {
        for (int i = 0; i < n; ++i) {
          ++stats_.sent_packets;
          stats_.sent_bytes +=
              static_cast<uint64_t>(frames[static_cast<size_t>(sent) + i].len);
        }
        sent += n;
        break;
      }

      if (n == -1 && errno == EINTR) {
        continue;
      }

      if (n == -1 && is_transient_send_error(errno)) {
        ++stats_.eagain;
        if (retry < kMaxSendRetries) {
          ++retry;
          backoff_after_send_retry(retry);
          continue;
        }
        std::fprintf(stderr, "sendmmsg failed after %d retries: %s\n",
                     kMaxSendRetries, std::strerror(errno));
        ++stats_.sent_errors;
        return sent;
      }

      if (n == -1) {
        std::fprintf(stderr, "sendmmsg failed: %s\n", std::strerror(errno));
        ++stats_.sent_errors;
      } else {
        ++stats_.sent_errors;
      }
      return sent;
    }
  }

  return sent;
#else
  

  for (size_t i = 0; i < count; ++i) {
    if (frames[i].data == nullptr || frames[i].len == 0) {
      ++stats_.sent_errors;
      break;
    }

    int retry = 0;
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
      if (n == -1 && is_transient_send_error(errno)) {
        ++stats_.eagain;
        if (retry < kMaxSendRetries) {
          ++retry;
          backoff_after_send_retry(retry);
          continue;
        }
        std::fprintf(stderr, "%s failed after %d retries: %s\n",
                     connected_ ? "send" : "sendto", kMaxSendRetries,
                     std::strerror(errno));
        ++stats_.sent_errors;
        return sent;
      }

      if (n >= 0 && n != static_cast<ssize_t>(frames[i].len)) {
        std::fprintf(stderr,
                     "%s returned short datagram write: expected=%zu actual=%zd\n",
                     connected_ ? "send" : "sendto", frames[i].len, n);
        ++stats_.sent_errors;
        return sent;
      }

      if (n == -1) {
        std::fprintf(stderr, "%s failed: %s\n",
                     connected_ ? "send" : "sendto", std::strerror(errno));
        ++stats_.sent_errors;
      } else {
        ++stats_.sent_errors;
      }
      return sent;
    }
  }

  return sent;
#endif
}

} // namespace net::udp::v2
