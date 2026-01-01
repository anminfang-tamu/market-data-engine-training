#pragma once
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cerrno>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net
{
    inline void die(const char *msg)
    {
        std::perror(msg);
        std::exit(1);
    }

    inline int make_listen_socket(uint16_t port)
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == -1)
            die("socket");

        int yes = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
            die("setsockopt");

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1)
            die("bind");

        if (listen(fd, SOMAXCONN) == -1)
            die("listen");

        return fd;
    }

    inline int accept_one(int listen_fd)
    {
        sockaddr_in client{};
        socklen_t len = sizeof(client);

        int cfd = -1;
        while (true)
        {
            cfd = accept(listen_fd, reinterpret_cast<sockaddr *>(&client), &len);
            if (cfd != -1)
                break;
            if (errno == EINTR)
                continue;
            die("accept");
        }

        return cfd;
    }

    inline ssize_t recv_exact(int cfd, void *buff, size_t len)
    {
        size_t total = 0;
        auto *p = static_cast<char *>(buff);

        while (total < len)
        {
            ssize_t n = recv(cfd, p + total, len - total, 0);
            if (n > 0)
            {
                total += static_cast<size_t>(n);
                continue;
            }
            if (n == 0)
            {
                return 0;
            }
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        return static_cast<ssize_t>(total);
    }

    template <typename OnMessage>
    inline bool handle_client(int cfd, size_t msg_len, OnMessage on_message)
    {
        char buff[4096]; // 4KB

        if (msg_len == 0 || msg_len > sizeof(buff))
            return false;

        while (true)
        {
            ssize_t n = recv_exact(cfd, buff, msg_len);
            if (n > 0)
                on_message(buff, static_cast<size_t>(n));
            if (n == 0)
            {
                break;
            }
            if (n < 0)
                return false;
        }

        return true;
    }

    template <typename OnMessage>
    inline void setup_server(int port, size_t msg_len, OnMessage on_message)
    {
        int listen_fd = make_listen_socket(port);
        std::cout << "Listening on " << port << "......" << std::endl;

        while (true)
        {
            int cfd = accept_one(listen_fd);
            handle_client(cfd, msg_len, on_message);
            close(cfd);
        }

        close(listen_fd);
    }
}
