#include <gtest/gtest.h>
#include <array>
#include <thread>
#include <atomic>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol/md_message.hpp"
#include "protocol/encode.hpp"
#include "protocol/decode.hpp"
#include "common/net/tcp_server_block.hpp"
#include "generator/generator.hpp"
#include "engine/normalize/normalize.hpp"

TEST(NormalizeTest, matchSize)
{
    EXPECT_TRUE(engine::isSizeMatch(protocol::kWireSize));
    EXPECT_FALSE(engine::isSizeMatch(protocol::kWireSize - 1));
    EXPECT_FALSE(engine::isSizeMatch(protocol::kWireSize + 1));
}

TEST(NormalizeTest, sanityCheck)
{
    // normal
    protocol::MarketDataMsg msg_ask_smaller_than_bid{1, 300, 10, 9, 100, 200};
    EXPECT_FALSE(engine::sanityCheck(msg_ask_smaller_than_bid));

    // negative ask size
    protocol::MarketDataMsg msg_ask_size_smaller_than_zero{1, 300, 10, 9, 100, -1};
    EXPECT_FALSE(engine::sanityCheck(msg_ask_size_smaller_than_zero));

    // negative bid size
    protocol::MarketDataMsg msg_bid_size_smaller_than_zero{1, 300, 10, 9, -1, 100};
    EXPECT_FALSE(engine::sanityCheck(msg_bid_size_smaller_than_zero));

    // negative bid price
    protocol::MarketDataMsg msg_negative_bid_price{1, 300, -1, 9, 101, 100};
    EXPECT_FALSE(engine::sanityCheck(msg_negative_bid_price));

    // negative ask price
    protocol::MarketDataMsg msg_negative_ask_price{1, 300, 10, -9, 101, 100};
    EXPECT_FALSE(engine::sanityCheck(msg_negative_ask_price));
}

TEST(NormalizeTest, decodeMsg)
{
    protocol::MarketDataMsg from{1, 300, 10, 11, 100, 200};
    auto bytes = protocol::encode(from);

    protocol::MarketDataMsg to{};
    bool decoded = engine::decode_msg(bytes.data(), bytes.size(), to);

    EXPECT_TRUE(decoded);
    EXPECT_EQ(sizeof(from), sizeof(to));
    EXPECT_EQ(to.symbol_id, from.symbol_id);
    EXPECT_EQ(to.exchange_ts, from.exchange_ts);
    EXPECT_EQ(to.bid_price, from.bid_price);
    EXPECT_EQ(to.ask_price, from.ask_price);
    EXPECT_EQ(to.bid_size, from.bid_size);
    EXPECT_EQ(to.ask_size, from.ask_size);
}