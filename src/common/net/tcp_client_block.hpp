#pragma once
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net
{
    inline void die(const char *msg, int fd)
    {
        if (fd >= 0)
        {
            close(fd);
        }
        perror(msg);
        exit(1);
    }

    inline int connect_to_server(const char *addr_in, uint16_t port)
    {
        // 1. create socket
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            die("socket", fd);

        // 2. setup server address
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        int addr_setup = inet_pton(AF_INET, addr_in, &addr.sin_addr);
        if (addr_setup == 0 || addr_setup == -1)
            die("setup server address", fd);

        // 3. connect to server
        if (connect(fd, (sockaddr *)&addr, sizeof(addr)) == -1)
            die("connect", fd);

        std::cout << "Connect to server" << std::endl;

        return fd;
    }

    inline bool send_all(int fd, const void *data, size_t len)
    {
        if (data == nullptr || len == 0)
            return true;

        size_t total = 0;
        const char *p = static_cast<const char *>(data);
        while (total < len)
        {
            ssize_t n = send(fd, p + total, len - total, 0);
            if (n > 0)
            {
                total += n;
                continue;
            }
            if (n == -1 && errno == EINTR)
            {
                continue;
            }
            return false;
        }

        return true;
    }
}