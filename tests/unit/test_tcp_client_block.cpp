#include <gtest/gtest.h>
#include <array>
#include <thread>
#include <atomic>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/net/tcp_server_block.hpp"
#include "common/net/tcp_client_block.hpp"
#include "protocol/encode.hpp"
#include "protocol/decode.hpp"

TEST(TcpClient, SendAllRecvLoop)
{
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    protocol::MarketDataMsg in{1, 123, 100, 101, 10, 12};
    auto bytes = protocol::encode(in);

    protocol::MarketDataMsg out{};

    std::thread reader([&]
                       { net::handle_client(
                             fds[1], protocol::kWireSize, [&](const void *data, size_t len)
                             { ASSERT_TRUE(protocol::decode(static_cast<const uint8_t *>(data), len, out)); }); });

    bool sent = net::send_all(fds[0], bytes.data(), bytes.size());
    shutdown(fds[0], SHUT_WR); // need to shutdown here to let reader see EOF

    EXPECT_TRUE(sent);

    reader.join();

    EXPECT_EQ(out.symbol_id, in.symbol_id);
    EXPECT_EQ(out.exchange_ts, in.exchange_ts);
    EXPECT_EQ(out.bid_price, in.bid_price);
    EXPECT_EQ(out.ask_price, in.ask_price);
    EXPECT_EQ(out.bid_size, in.bid_size);
    EXPECT_EQ(out.ask_size, in.ask_size);

    close(fds[0]);
    close(fds[1]);
}

TEST(TcpClient, ConnectToServer)
{
    std::atomic<uint16_t> port{0};
    std::atomic<int> server_fd{-1};
    std::atomic<int> client_fd{-1};

    std::thread server([&]
                       {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(fd, -1);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);

        ASSERT_EQ(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

        socklen_t len = sizeof(addr);
        ASSERT_EQ(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len), 0);
        port.store(ntohs(addr.sin_port), std::memory_order_release);

        ASSERT_EQ(listen(fd, 1), 0);
        server_fd.store(fd, std::memory_order_release);

        int cfd = accept(fd, nullptr, nullptr);
        if (cfd >= 0) {
            client_fd.store(cfd, std::memory_order_release);
            close(cfd);
        }
        close(fd); });

    while (port.load(std::memory_order_acquire) == 0)
    {
        std::this_thread::yield();
    }

    int fd = net::connect_to_server("127.0.0.1", port.load());
    EXPECT_GE(fd, 0);
    if (fd >= 0)
    {
        close(fd);
    }

    server.join();
}