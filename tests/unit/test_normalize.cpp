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

class NormalizeTest : public ::testing::Test
{
protected:
    static inline std::atomic<uint16_t> port_{8888};
    static inline std::thread server_thread_;
    static inline std::atomic<bool> ready_{false};
    static inline std::atomic<bool> stop_{false};
    static inline std::vector<protocol::MarketDataMsg> received;
    static inline std::atomic<bool> server_ok_{true};
    static inline std::mutex mutex_;

    static void SetUpTestSuite()
    {
        server_thread_ = std::thread([]
                                     {
                                         int fd = socket(AF_INET, SOCK_STREAM, 0);
                                         if (fd < 0)
                                             return;

                                         sockaddr_in addr{};
                                         addr.sin_family = AF_INET;
                                         addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                                         addr.sin_port = htons(port_.load());

                                         if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
                                         {
                                             close(fd);
                                             return;
                                         }

                                         socklen_t len = sizeof(addr);
                                         if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0)
                                         {
                                             close(fd);
                                             return;
                                         }

                                         if (listen(fd, 1) != 0)
                                         {
                                             close(fd);
                                             return;
                                         }

                                         ready_.store(true, std::memory_order_release);

                                         while (!stop_.load(std::memory_order_acquire))
                                         {
                                            int cfd = accept(fd, nullptr, nullptr);
                                            if (cfd < 0)
                                            {
                                                continue;
                                            }
                                            

                                            std::array<uint8_t, protocol::kWireSize> buff{};
                                            while(true)
                                            {
                                                ssize_t n = net::recv_exact(cfd, buff.data(), buff.size());
                                                if (n <= 0) break;

                                                protocol::MarketDataMsg msg{};
                                                if (protocol::decode(buff.data(), buff.size(), msg)) {
                                                    std::lock_guard<std::mutex> lock(mutex_);
                                                    received.push_back(msg);
                                                } else {
                                                    server_ok_.store(false);
                                                    break;
                                                }
                                            }
                                            close(cfd);
                                         }
                                         close(fd); });

        while (!ready_.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        received.clear();
    }

    static void TearDownTestSuite()
    {
        stop_.store(true, std::memory_order_release);

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0)
        {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(port_.load(std::memory_order_acquire));
            connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
            close(fd);
        }

        if (server_thread_.joinable())
        {
            server_thread_.join();
        }
    }
};

TEST_F(NormalizeTest, matchSize)
{
    protocol::MarketDataMsg msg{1, 123, 10, 11, 20, 30};
    generator::Generator gen;
    bool connected = gen.connect("127.0.0.1", port_.load());
    gen.send(msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_EQ(sizeof(msg), sizeof(received[0]));
}

TEST_F(NormalizeTest, notMatchSize)
{
    EXPECT_TRUE(engine::isSizeMatch(protocol::kWireSize));
    EXPECT_FALSE(engine::isSizeMatch(protocol::kWireSize - 1));
    EXPECT_FALSE(engine::isSizeMatch(protocol::kWireSize + 1));
}

TEST_F(NormalizeTest, sanityCheck)
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

TEST_F(NormalizeTest, decodeMsg)
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