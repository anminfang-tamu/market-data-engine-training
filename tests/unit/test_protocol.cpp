#include <gtest/gtest.h>
#include "md_message.hpp"
#include "encode.hpp"
#include "decode.hpp"

TEST(Protocol, RoundTrip)
{
    protocol::MarketDataMsg from{1, 123, 100, 101, 10, 12};
    auto bytes = protocol::encode(from);

    protocol::MarketDataMsg to{};
    ASSERT_TRUE(protocol::decode(bytes.data(), bytes.size(), to));
    EXPECT_EQ(to.symbol_id, from.symbol_id);
    EXPECT_EQ(to.exchange_ts, from.exchange_ts);
    EXPECT_EQ(to.bid_price, from.bid_price);
    EXPECT_EQ(to.ask_price, from.ask_price);
    EXPECT_EQ(to.bid_size, from.bid_size);
    EXPECT_EQ(to.ask_size, from.ask_size);
}

TEST(Protocol, RoundTripWithMsgZero)
{
    protocol::MarketDataMsg from{0, 0, 0, 0, 0, 0};
    auto bytes = protocol::encode(from);

    protocol::MarketDataMsg to{};
    ASSERT_TRUE(protocol::decode(bytes.data(), bytes.size(), to));
    EXPECT_EQ(to.symbol_id, from.symbol_id);
    EXPECT_EQ(to.exchange_ts, from.exchange_ts);
    EXPECT_EQ(to.bid_price, from.bid_price);
    EXPECT_EQ(to.ask_price, from.ask_price);
    EXPECT_EQ(to.bid_size, from.bid_size);
    EXPECT_EQ(to.ask_size, from.ask_size);
}

TEST(Protocol, RoundTripWithMsgMax)
{
    protocol::MarketDataMsg from{UINT32_MAX, INT64_MAX, INT64_MAX, INT64_MAX, UINT32_MAX, UINT32_MAX};
    auto bytes = protocol::encode(from);

    protocol::MarketDataMsg to{};
    ASSERT_TRUE(protocol::decode(bytes.data(), bytes.size(), to));
    EXPECT_EQ(to.symbol_id, from.symbol_id);
    EXPECT_EQ(to.exchange_ts, from.exchange_ts);
    EXPECT_EQ(to.bid_price, from.bid_price);
    EXPECT_EQ(to.ask_price, from.ask_price);
    EXPECT_EQ(to.bid_size, from.bid_size);
    EXPECT_EQ(to.ask_size, from.ask_size);
}

TEST(Protocol, RoundTripWithMsgMin)
{
    protocol::MarketDataMsg from{UINT32_MAX, INT64_MIN, INT64_MIN, INT64_MIN, UINT32_MAX, UINT32_MAX};
    auto bytes = protocol::encode(from);

    protocol::MarketDataMsg to{};
    ASSERT_TRUE(protocol::decode(bytes.data(), bytes.size(), to));
    EXPECT_EQ(to.symbol_id, from.symbol_id);
    EXPECT_EQ(to.exchange_ts, from.exchange_ts);
    EXPECT_EQ(to.bid_price, from.bid_price);
    EXPECT_EQ(to.ask_price, from.ask_price);
    EXPECT_EQ(to.bid_size, from.bid_size);
    EXPECT_EQ(to.ask_size, from.ask_size);
}

TEST(Protocol, RoundTripWithMsgNegative)
{
    protocol::MarketDataMsg from{UINT32_MAX, INT64_MIN, -1, 1, UINT32_MAX, UINT32_MAX};
    auto bytes = protocol::encode(from);

    protocol::MarketDataMsg to{};
    ASSERT_TRUE(protocol::decode(bytes.data(), bytes.size(), to));
    EXPECT_EQ(to.symbol_id, from.symbol_id);
    EXPECT_EQ(to.exchange_ts, from.exchange_ts);
    EXPECT_EQ(to.bid_price, from.bid_price);
    EXPECT_EQ(to.ask_price, from.ask_price);
    EXPECT_EQ(to.bid_size, from.bid_size);
    EXPECT_EQ(to.ask_size, from.ask_size);
}

TEST(Protocol, RoundTripWithTwiceDuplicateMsg)
{
    protocol::MarketDataMsg from1{1, 123, 100, 101, 10, 12};
    protocol::MarketDataMsg from2{1, 123, 100, 101, 10, 12};

    auto bytes1 = protocol::encode(from1);
    auto bytes2 = protocol::encode(from2);

    EXPECT_EQ(bytes1, bytes2);
}

TEST(Protocol, ByteCorrupt)
{
    protocol::MarketDataMsg from{1, 123, 100, 101, 10, 12};
    auto bytes = protocol::encode(from);
    auto bytes_corrupted = bytes;

    bytes_corrupted[0] ^= 0x01;

    auto equal_msg = [](const protocol::MarketDataMsg &a, const protocol::MarketDataMsg &b)
    {
        return a.symbol_id == b.symbol_id && a.bid_price == b.bid_price && a.ask_price == b.ask_price && a.bid_size == b.bid_size && a.ask_size == b.ask_size;
    };

    protocol::MarketDataMsg to{};
    ASSERT_TRUE(protocol::decode(bytes_corrupted.data(), bytes_corrupted.size(), to));
    EXPECT_FALSE(equal_msg(to, from));
}

TEST(Protocol, DecodeSizeNotMatch)
{
    protocol::MarketDataMsg from{1, 123, 100, 101, 10, 12};
    auto bytes = protocol::encode(from);

    protocol::MarketDataMsg to{};
    ASSERT_FALSE(protocol::decode(bytes.data(), bytes.size() - 1, to));
    // ignore tail, shoule be true
    ASSERT_TRUE(protocol::decode(bytes.data(), bytes.size() + 5, to));
}