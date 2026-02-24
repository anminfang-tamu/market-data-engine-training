#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include "common/metrics/metrics.hpp"
#include "common/thread/affinity.hpp"
#include "containers/frame_pool.hpp"
#include "protocol/encode.hpp"

namespace net::udp {

struct ServerHandle {
  std::atomic<bool> running{false};
  int sock_fd{-1};
  int epfd{-1};
  int wake_fd{-1};
  std::thread io_thread;
};

inline int make_recv_socket(int port, const char *bind_ip = "0.0.0.0",
                            int rcvbuf_bytes = 0) {
  int sock_fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (sock_fd == -1) {
    std::perror("socket");
    return -1;
  }

  int yes = 1;
  if (::setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
    std::perror("setsockopt SO_REUSEADDR");
    ::close(sock_fd);
    return -1;
  }

  if (rcvbuf_bytes > 0) {
    if (::setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_bytes,
                     sizeof(rcvbuf_bytes)) == -1) {
      std::perror("setsockopt SO_RCVBUF");
      ::close(sock_fd);
      return -1;
    }
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  if (bind_ip == nullptr || bind_ip[0] == '\0') {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (::inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1) {
    std::perror("inet_pton");
    ::close(sock_fd);
    return -1;
  }

  if (::bind(sock_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
    std::perror("bind");
    ::close(sock_fd);
    return -1;
  }

  return sock_fd;
}

template <size_t Capacity>
inline void run_server_loop(ServerHandle &handle,
                            containers::FramePool<Capacity> &pool,
                            metrics::Metrics &metrics, int port,
                            const char *bind_ip = "0.0.0.0",
                            int rcvbuf_bytes = 0) {
  handle.sock_fd = make_recv_socket(port, bind_ip, rcvbuf_bytes);
  if (handle.sock_fd == -1) {
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  handle.epfd = ::epoll_create1(EPOLL_CLOEXEC);
  if (handle.epfd == -1) {
    std::perror("epoll_create1");
    ::close(handle.sock_fd);
    handle.sock_fd = -1;
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  handle.wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (handle.wake_fd == -1) {
    std::perror("eventfd");
    ::close(handle.epfd);
    ::close(handle.sock_fd);
    handle.epfd = -1;
    handle.sock_fd = -1;
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  epoll_event sev{};
  sev.events = EPOLLIN | EPOLLET;
  sev.data.fd = handle.sock_fd;
  if (::epoll_ctl(handle.epfd, EPOLL_CTL_ADD, handle.sock_fd, &sev) == -1) {
    std::perror("epoll_ctl sock");
    ::close(handle.wake_fd);
    ::close(handle.epfd);
    ::close(handle.sock_fd);
    handle.wake_fd = -1;
    handle.epfd = -1;
    handle.sock_fd = -1;
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  epoll_event wev{};
  wev.events = EPOLLIN | EPOLLET;
  wev.data.fd = handle.wake_fd;
  if (::epoll_ctl(handle.epfd, EPOLL_CTL_ADD, handle.wake_fd, &wev) == -1) {
    std::perror("epoll_ctl wake");
    ::close(handle.wake_fd);
    ::close(handle.epfd);
    ::close(handle.sock_fd);
    handle.wake_fd = -1;
    handle.epfd = -1;
    handle.sock_fd = -1;
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  constexpr int MAXEV = 32;
  epoll_event events[MAXEV];

  constexpr int BATCH = 32;
  constexpr size_t BUF_SIZE = 2048;
  alignas(64) mmsghdr msgs[BATCH];
  alignas(64) iovec iov[BATCH];
  alignas(64) uint8_t io_buf[BATCH][BUF_SIZE];

  for (int i = 0; i < BATCH; ++i) {
    std::memset(&msgs[i], 0, sizeof(mmsghdr));
    std::memset(&iov[i], 0, sizeof(iovec));
    iov[i].iov_base = io_buf[i];
    iov[i].iov_len = BUF_SIZE;
    msgs[i].msg_hdr.msg_iov = &iov[i];
    msgs[i].msg_hdr.msg_iovlen = 1;
    msgs[i].msg_hdr.msg_name = nullptr;
    msgs[i].msg_hdr.msg_namelen = 0;
    msgs[i].msg_hdr.msg_control = nullptr;
    msgs[i].msg_hdr.msg_controllen = 0;
    msgs[i].msg_hdr.msg_flags = 0;
    msgs[i].msg_len = 0;
  }

  while (handle.running.load(std::memory_order_relaxed)) {
    const int nfd = ::epoll_wait(handle.epfd, events, MAXEV, 500);
    if (nfd == -1) {
      if (errno == EINTR) {
        continue;
      }
      std::perror("epoll_wait");
      break;
    }
    if (nfd == 0) {
      continue;
    }

    for (int i = 0; i < nfd; ++i) {
      const int fd = events[i].data.fd;
      const uint32_t evs = events[i].events;

      if (fd == handle.wake_fd) {
        uint64_t tmp = 0;
        while (::read(handle.wake_fd, &tmp, sizeof(tmp)) == sizeof(tmp)) {
        }
        handle.running.store(false, std::memory_order_relaxed);
        continue;
      }

      if (fd != handle.sock_fd) {
        continue;
      }

      if (evs & (EPOLLERR | EPOLLHUP)) {
        std::perror("udp socket epoll error");
        handle.running.store(false, std::memory_order_relaxed);
        break;
      }

      for (;;) {
        const int n = ::recvmmsg(handle.sock_fd, msgs, BATCH, MSG_DONTWAIT, nullptr);
        if (n > 0) {
          for (int j = 0; j < n; ++j) {
            const size_t len = static_cast<size_t>(msgs[j].msg_len);
            const bool truncated = (msgs[j].msg_hdr.msg_flags & MSG_TRUNC) != 0;
            if (truncated || len != protocol::kWireSize) {
              metrics.inc_drops();
              msgs[j].msg_hdr.msg_flags = 0;
              msgs[j].msg_len = 0;
              continue;
            }

            size_t idx = 0;
            if (!pool.free.pop(idx)) {
              metrics.inc_drops();
              msgs[j].msg_hdr.msg_flags = 0;
              msgs[j].msg_len = 0;
              continue;
            }

            std::memcpy(pool.slots[idx].data(), io_buf[j], protocol::kWireSize);
            if (!pool.ready.push(idx)) {
              metrics.inc_drops();
              pool.free.push(idx);
            }

            msgs[j].msg_hdr.msg_flags = 0;
            msgs[j].msg_len = 0;
          }
          continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          break;
        }
        if (n == -1 && errno == EINTR) {
          continue;
        }
        if (n == -1) {
          std::perror("recvmmsg");
        }
        break;
      }
    }
  }

  if (handle.epfd != -1) {
    ::close(handle.epfd);
    handle.epfd = -1;
  }
  if (handle.sock_fd != -1) {
    ::close(handle.sock_fd);
    handle.sock_fd = -1;
  }
  if (handle.wake_fd != -1) {
    ::close(handle.wake_fd);
    handle.wake_fd = -1;
  }
}

template <size_t Capacity>
inline bool start_server(ServerHandle &handle,
                         containers::FramePool<Capacity> &pool,
                         metrics::Metrics &metrics, int port, int cpu_id = -1,
                         const char *bind_ip = "0.0.0.0",
                         int rcvbuf_bytes = 0) {
  if (handle.running.load(std::memory_order_relaxed)) {
    return false;
  }

  const std::string bind_ip_copy = (bind_ip == nullptr) ? "" : bind_ip;
  handle.running.store(true, std::memory_order_relaxed);
  handle.io_thread = std::thread(
      [&handle, &pool, &metrics, port, cpu_id, bind_ip_copy, rcvbuf_bytes] {
        if (cpu_id >= 0) {
          common::thread::pin_current_thread(cpu_id);
        }
        run_server_loop<Capacity>(handle, pool, metrics, port, bind_ip_copy.c_str(),
                                  rcvbuf_bytes);
      });
  return true;
}

inline void stop_server(ServerHandle &handle) {
  if (!handle.running.exchange(false, std::memory_order_relaxed)) {
    return;
  }

  if (handle.wake_fd != -1) {
    const uint64_t one = 1;
    ::write(handle.wake_fd, &one, sizeof(one));
  }

  if (handle.io_thread.joinable()) {
    handle.io_thread.join();
  }
}

} // namespace net::udp
