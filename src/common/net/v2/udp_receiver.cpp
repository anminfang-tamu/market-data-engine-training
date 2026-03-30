#include "udp_receiver.hpp"

#include <cerrno>
#include <cstdio>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <sys/epoll.h>
#else
#include <poll.h>
#endif

namespace net::udp::v2 {

namespace {
void reset_frame(RxFrame &frame) {
  frame.len = 0;
  frame.peer = {};
  frame.truncated = false;
}
} // namespace

Receiver::Receiver(Receiver &&other) noexcept
    : fd_(std::exchange(other.fd_, -1)), epfd_(std::exchange(other.epfd_, -1)),
      connected_(std::exchange(other.connected_, false)),
      remote_(std::exchange(other.remote_, sockaddr_in{})),
      stats_(other.stats_) {}

Receiver &Receiver::operator=(Receiver &&other) noexcept {
  if (this == &other) {
    return *this;
  }

  close();
  fd_ = std::exchange(other.fd_, -1);
  epfd_ = std::exchange(other.epfd_, -1);
  connected_ = std::exchange(other.connected_, false);
  remote_ = std::exchange(other.remote_, sockaddr_in{});
  stats_ = other.stats_;
  return *this;
}

bool Receiver::open(const ReceiverConfig &cfg) {
  // close all previous connections
  close();

  // check connection configs
  if (cfg.local_port == 0) {
    return false;
  }
  if (cfg.connect_socket &&
      (cfg.remote_ip == nullptr || cfg.remote_ip[0] == '\0' ||
       cfg.remote_port == 0)) {
    return false;
  }

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

  int yes = 1;
  if (::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    std::perror("setsockopt SO_REUSEADDR");
    ::close(sockfd);
    return false;
  }

  if (cfg.rcvbuf_bytes > 0) {
    if (::setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &cfg.rcvbuf_bytes,
                     sizeof(cfg.rcvbuf_bytes)) == -1) {
      std::perror("setsockopt SO_RCVBUF");
      ::close(sockfd);
      return false;
    }
  }

  sockaddr_in local_addr{};
  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons(cfg.local_port);

  // inet_pton: convert addr from string to binary network
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
  if (cfg.connect_socket) {
    remote_.sin_family = AF_INET;
    remote_.sin_port = htons(cfg.remote_port);
    if (::inet_pton(AF_INET, cfg.remote_ip, &remote_.sin_addr) != 1) {
      std::perror("inet_pton remote_ip");
      ::close(sockfd);
      remote_ = {};
      return false;
    }

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

void Receiver::close() {
  if (epfd_ >= 0) {
    ::close(epfd_);
  }
  if (fd_ >= 0) {
    ::close(fd_);
  }

  fd_ = -1;
  epfd_ = -1;
  connected_ = false;
  remote_ = {};
}

bool Receiver::receive_one(RxFrame &frame) {
  reset_frame(frame);

  if (fd_ < 0 || frame.data == nullptr || frame.capacity == 0) {
    ++stats_.received_errors;
    return false;
  }

  iovec iov{};
  iov.iov_base = frame.data;
  iov.iov_len = frame.capacity;

  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  sockaddr_in peer{};
  if (!connected_) {
    msg.msg_name = &peer;
    msg.msg_namelen = sizeof(peer);
  }

  for (;;) {
    const ssize_t n = recvmsg(fd_, &msg, MSG_DONTWAIT);
    if (n >= 0) {
      frame.len = n;
      frame.truncated = (msg.msg_flags & MSG_TRUNC) != 0;
      frame.peer = connected_ ? remote_ : peer;
      ++stats_.received_packets;
      stats_.received_bytes += static_cast<uint64_t>(frame.len);
      return true;
    }

    // system interupt
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      ++stats_.eagain;
    } else {
      ++stats_.received_errors;
    }
    return false;
  }
}

int Receiver::receive_batch(RxFrame *frames, size_t count) {
  if (fd_ < 0 || count == 0) {
    return -1;
  }

  if (frames == nullptr) {
    ++stats_.received_errors;
    return -1;
  }

  thread_local std::vector<mmsghdr> msgs;
  thread_local std::vector<iovec> iovecs;
  thread_local std::vector<sockaddr_in> peers;

  if (msgs.size() < count) {
    msgs.resize(count);
    iovecs.resize(count);
    peers.resize(count);
  }

  for (size_t i = 0; i < count; ++i) {
    reset_frame(frames[i]);
    if (frames[i].data == nullptr || frames[i].capacity == 0) {
      ++stats_.received_errors;
      return -1;
    }

    msgs[i] = {};
    iovecs[i].iov_base = frames[i].data;
    iovecs[i].iov_len = frames[i].capacity;
    msgs[i].msg_hdr.msg_iov = &iovecs[i];
    msgs[i].msg_hdr.msg_iovlen = 1;

    if (!connected_) {
      peers[i] = {};
      msgs[i].msg_hdr.msg_name = &peers[i];
      msgs[i].msg_hdr.msg_namelen = sizeof(peers[i]);
    }
  }

  for (;;) {
    const int n = ::recvmmsg(fd_, msgs.data(), static_cast<unsigned int>(count),
                             MSG_DONTWAIT, nullptr);
    if (n > 0) {
      for (int i = 0; i < n; ++i) {
        frames[i].len = static_cast<size_t>(msgs[i].msg_len);
        frames[i].truncated = (msgs[i].msg_hdr.msg_flags & MSG_TRUNC) != 0;
        frames[i].peer = connected_ ? remote_ : peers[i];
        ++stats_.received_packets;
        stats_.received_bytes += static_cast<uint64_t>(frames[i].len);
      }
      return n;
    }

    if (n == -1 && errno == EINTR) {
      continue;
    }
    if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      ++stats_.eagain;
      return 0;
    }

    if (n == -1) {
      ++stats_.received_errors;
    }
    return -1;
  }
}

int Receiver::epoll_receive(RxFrame *frames, size_t count, int timeout_ms) {
  if (!ensure_epoll()) {
    ++stats_.received_errors;
    return -1;
  }

  epoll_event event{};
  for (;;) {
    // to receive 32 packets once a time
    const int n = ::epoll_wait(epfd_, &event, 1, timeout_ms);
    if (n > 0) {
      if (event.events & (EPOLLERR | EPOLLHUP)) {
        ++stats_.received_errors;
        return -1;
      }
      return receive_batch(frames, count);
    }

    if (n == 0) {
      return 0;
    }
    if (errno == EINTR) {
      continue;
    }

    ++stats_.received_errors;
    return -1;
  }
}

bool Receiver::ensure_epoll() {
  if (fd_ < 0) {
    return false;
  }

#if defined(__linux__)
  if (epfd_ >= 0) {
    return true;
  }

  const int epfd = ::epoll_create1(EPOLL_CLOEXEC);
  if (epfd == -1) {
    std::perror("epoll_create1");
    return false;
  }

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = fd_;
  if (::epoll_ctl(epfd, EPOLL_CTL_ADD, fd_, &event) == -1) {
    std::perror("epoll_ctl");
    ::close(epfd);
    return false;
  }

  epfd_ = epfd;

  return true;
#else
  return true;
#endif
}
} // namespace net::udp::v2
