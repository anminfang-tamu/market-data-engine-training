#include <gtest/gtest.h>
#include <array>
#include <thread>
#include <atomic>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "generator/generator.hpp"
#include "common/net/tcp_server_block.hpp"
#include "common/net/tcp_client_block.hpp"
#include "protocol/encode.hpp"
#include "protocol/decode.hpp"

class GeneratorTest : public ::testing::Test
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

TEST_F(GeneratorTest, Connect)
{
    generator::Generator gen;
    bool connected = gen.connect("127.0.0.1", port_.load());
    EXPECT_TRUE(connected);
}

TEST_F(GeneratorTest, SendAndReceive)
{
    generator::Generator gen;
    bool connected = gen.connect("127.0.0.1", port_.load());
    EXPECT_TRUE(connected);

    protocol::MarketDataMsg in1{1, 123, 100, 101, 10, 12};
    protocol::MarketDataMsg in2{2, 124, 110, 102, 11, 15};
    protocol::MarketDataMsg in3{3, 125, 120, 103, 12, 19};
    std::vector<protocol::MarketDataMsg> expected;
    expected.push_back(in1);
    expected.push_back(in2);
    expected.push_back(in3);

    for (int i = 0; i < 3; ++i)
    {
        bool sent = gen.send(expected[i]);
        EXPECT_TRUE(sent);
    }

    for (int i = 0; i < 100 && received.size() < expected.size(); ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_EQ(received.size(), expected.size());
    for (int i = 0; i < received.size(); ++i)
    {
        EXPECT_EQ(received[i].symbol_id, expected[i].symbol_id);
        EXPECT_EQ(received[i].exchange_ts, expected[i].exchange_ts);
        EXPECT_EQ(received[i].bid_price, expected[i].bid_price);
        EXPECT_EQ(received[i].ask_price, expected[i].ask_price);
        EXPECT_EQ(received[i].bid_size, expected[i].bid_size);
        EXPECT_EQ(received[i].ask_size, expected[i].ask_size);
    }
}

TEST_F(GeneratorTest, RunWithRateZero)
{
    generator::Generator gen;
    bool connected = gen.connect("127.0.0.1", port_.load());
    EXPECT_TRUE(connected);
    bool ran = gen.run(10, 0, 1);

    for (int i = 0; i < 100; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_EQ(received.size(), 10);
}

TEST_F(GeneratorTest, RunWithRateLargerThanZero)
{
    generator::Generator gen;
    bool connected = gen.connect("127.0.0.1", port_.load());
    EXPECT_TRUE(connected);
    bool ran = gen.run(10, 1, 1);

    for (int i = 0; i < 100; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_EQ(received.size(), 10);
}