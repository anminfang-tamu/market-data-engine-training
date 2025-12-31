#include <gtest/gtest.h>
#include <array>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>

#include "common/net/tcp_server.hpp"
#include "protocol/encode.hpp"
#include "protocol/decode.hpp"

TEST(TcpServer, RecvExactReadsFullBuffer)
{
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    std::array<uint8_t, protocol::kWireSize> payload{};
    for (size_t i = 0; i < payload.size(); ++i)
    {
        payload[i] = static_cast<uint8_t>(i & 0xFF);
    }

    std::thread writer([&]
                       {
        // send in two chunks to force partial read
        send(fds[1], payload.data(), 5, 0);
        send(fds[1], payload.data() + 5, payload.size() - 5, 0);
        shutdown(fds[1], SHUT_WR); });

    std::array<uint8_t, protocol::kWireSize> out{};
    ssize_t n = net::recv_exact(fds[0], out.data(), out.size());
    EXPECT_EQ(n, static_cast<ssize_t>(out.size()));
    EXPECT_EQ(out, payload);

    writer.join();
    close(fds[0]);
    close(fds[1]);
}

TEST(TcpServer, HandleClientCallsCallback)
{
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

    protocol::MarketDataMsg msg{1, 123, 100, 101, 10, 12};
    auto bytes = protocol::encode(msg);

    int count = 0;

    std::thread writer([&]
                       {
        for (int i = 0; i < 3; ++i) {
            send(fds[1], bytes.data(), bytes.size(), 0);
        }
        shutdown(fds[1], SHUT_WR); });

    bool ok = net::handle_client(
        fds[0],
        protocol::kWireSize,
        [&](const void *data, size_t len)
        {
            protocol::MarketDataMsg out{};
            ASSERT_TRUE(protocol::decode(static_cast<const uint8_t *>(data), len, out));
            EXPECT_EQ(out.symbol_id, msg.symbol_id);
            ++count;
        });

    EXPECT_TRUE(ok);
    EXPECT_EQ(count, 3);

    writer.join();
    close(fds[0]);
    close(fds[1]);
}
