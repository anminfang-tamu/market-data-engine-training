#include <gtest/gtest.h>
#include <array>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>

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