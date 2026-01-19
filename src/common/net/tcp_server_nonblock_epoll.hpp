#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "common/metrics/metrics.hpp"
#include "containers/frame_pool.hpp"

// Only reassembles frames; no decode here.

namespace net {

struct ServerHandle {
  std::atomic<bool> running{false};
  int listen_fd{-1};
  int epfd{-1};
  int wake_fd{-1};
  std::thread io_thread;
};

struct ConnState {
  int fd{-1};
  size_t have{0};
  uint8_t frame[protocol::kWireSize];
};

inline int make_listen_socket(int port) {
  int listen_fd =
      ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (listen_fd == -1) {
    perror("socket");
    return -1;
  }

  int yes = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) ==
      -1) {
    perror("setsockopt");
    ::close(listen_fd);
    return -1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    perror("inet_pton");
    ::close(listen_fd);
    return -1;
  }

  if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
      -1) {
    perror("bind");
    ::close(listen_fd);
    return -1;
  }

  if (listen(listen_fd, SOMAXCONN) == -1) {
    perror("listen");
    ::close(listen_fd);
    return -1;
  }

  return listen_fd;
}

// Reassemble fixed-size frames from a stream and enqueue buffer indices.
template <size_t Capacity>
inline void consume_frames(ConnState &st, const uint8_t *data, size_t n,
                           containers::FramePool<Capacity> &pool,
                           metrics::Metrics &metrics) {
  size_t off = 0;
  while (off < n) {
    const size_t need = protocol::kWireSize - st.have;
    const size_t take = (n - off < need) ? (n - off) : need;
    std::memcpy(st.frame + st.have, data + off, take);
    st.have += take;
    off += take;

    if (st.have == protocol::kWireSize) {
      size_t idx = 0;
      if (!pool.free.pop(idx)) {
        metrics.inc_drops();
        st.have = 0;
        continue;
      }
      std::memcpy(pool.slots[idx].data(), st.frame, protocol::kWireSize);
      pool.ready.push(idx);
      st.have = 0;
    }
  }
}

template <size_t Capacity>
inline void run_server_loop(ServerHandle &handle,
                            containers::FramePool<Capacity> &pool,
                            metrics::Metrics &metrics, int port) {
  handle.listen_fd = make_listen_socket(port);
  if (handle.listen_fd == -1) {
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  handle.epfd = epoll_create1(EPOLL_CLOEXEC);
  if (handle.epfd == -1) {
    perror("epoll_create1");
    ::close(handle.listen_fd);
    handle.listen_fd = -1;
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  handle.wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (handle.wake_fd == -1) {
    perror("eventfd");
    close(handle.epfd);
    close(handle.listen_fd);
    handle.epfd = -1;
    handle.listen_fd = -1;
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  epoll_event lev{};
  lev.events = EPOLLIN | EPOLLET;
  lev.data.fd = handle.listen_fd;
  if (epoll_ctl(handle.epfd, EPOLL_CTL_ADD, handle.listen_fd, &lev) == -1) {
    perror("epoll_ctl listen");
    close(handle.wake_fd);
    close(handle.epfd);
    close(handle.listen_fd);
    handle.wake_fd = -1;
    handle.epfd = -1;
    handle.listen_fd = -1;
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  epoll_event wev{};
  wev.events = EPOLLIN | EPOLLET;
  wev.data.fd = handle.wake_fd;
  if (epoll_ctl(handle.epfd, EPOLL_CTL_ADD, handle.wake_fd, &wev) == -1) {
    perror("epoll_ctl wake");
    close(handle.wake_fd);
    close(handle.epfd);
    close(handle.listen_fd);
    handle.wake_fd = -1;
    handle.epfd = -1;
    handle.listen_fd = -1;
    handle.running.store(false, std::memory_order_relaxed);
    return;
  }

  // Simple fd-indexed state; limit to avoid giant arrays.
  constexpr int MAX_FD = 1 << 16;
  static ConnState conns[MAX_FD];

  auto init_conn = [&](int fd) -> bool {
    if (fd < 0 || fd >= MAX_FD)
      return false;
    conns[fd].fd = fd;
    conns[fd].have = 0;
    return true;
  };

  auto reset_conn = [&](int fd) {
    if (fd >= 0 && fd < MAX_FD) {
      conns[fd].fd = -1;
      conns[fd].have = 0;
    }
  };

  constexpr int MAXEV = 64;
  epoll_event events[MAXEV];
  uint8_t io_buf[64 * 1024];

  while (handle.running.load(std::memory_order_relaxed)) {
    int nfd = epoll_wait(handle.epfd, events, MAXEV, 500);
    if (nfd == -1) {
      if (errno == EINTR)
        continue;
      perror("epoll_wait");
      break;
    }
    if (nfd == 0) {
      continue;
    }

    for (int i = 0; i < nfd; ++i) {
      const int fd = events[i].data.fd;
      const uint32_t evs = events[i].events;

      if (fd == handle.wake_fd) {
        uint64_t tmp;
        read(handle.wake_fd, &tmp, sizeof(tmp)); // drain
        handle.running.store(false, std::memory_order_relaxed);
        continue;
      }

      if (fd == handle.listen_fd) {
        // accept loop
        for (;;) {
          int cfd = accept4(handle.listen_fd, nullptr, nullptr,
                            SOCK_NONBLOCK | SOCK_CLOEXEC);
          if (cfd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            if (errno == EINTR)
              continue;
            perror("accept4");
            break;
          }

          if (!init_conn(cfd)) {
            ::close(cfd);
            continue;
          }

          epoll_event cev{};
          cev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
          cev.data.fd = cfd;
          if (epoll_ctl(handle.epfd, EPOLL_CTL_ADD, cfd, &cev) == -1) {
            perror("epoll_ctl add client");
            close(cfd);
            reset_conn(cfd);
          }
        }
        continue;
      }

      if (evs & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
        close(fd);
        reset_conn(fd);
        continue;
      }

      // recv loop: drain until EAGAIN
      for (;;) {
        ssize_t n = ::recv(fd, io_buf, sizeof(io_buf), 0);
        if (n > 0) {
          if (fd >= 0 && fd < MAX_FD && conns[fd].fd == fd) {
            consume_frames<Capacity>(conns[fd], io_buf, static_cast<size_t>(n),
                                     pool, metrics);
          }
          continue;
        }

        if (n == 0) {
          ::close(fd);
          reset_conn(fd);
          break;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        if (errno == EINTR)
          continue;

        perror("recv");
        close(fd);
        reset_conn(fd);
        break;
      }
    }
  }

  if (handle.epfd != -1) {
    close(handle.epfd);
    handle.epfd = -1;
  }
  if (handle.listen_fd != -1) {
    close(handle.listen_fd);
    handle.listen_fd = -1;
  }
  if (handle.wake_fd != -1) {
    close(handle.wake_fd);
    handle.wake_fd = -1;
  }
}

// Start server in background thread
template <size_t Capacity>
inline bool start_server(ServerHandle &handle,
                         containers::FramePool<Capacity> &pool,
                         metrics::Metrics &metrics, int port) {
  if (handle.running.load(std::memory_order_relaxed))
    return false;
  handle.running.store(true, std::memory_order_relaxed);

  handle.io_thread = std::thread(
      [&handle, &pool, &metrics, port] {
        run_server_loop<Capacity>(handle, pool, metrics, port);
      });
  return true;
}

// Stop server and join thread
inline void stop_server(ServerHandle &handle) {
  if (!handle.running.exchange(false, std::memory_order_relaxed))
    return;

  if (handle.wake_fd != -1) {
    uint64_t one = 1;
    write(handle.wake_fd, &one, sizeof(one)); // wake epoll_wait
  }

  if (handle.io_thread.joinable())
    handle.io_thread.join();
}

} // namespace net
