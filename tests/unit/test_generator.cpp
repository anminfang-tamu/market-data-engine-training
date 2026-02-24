#include <gtest/gtest.h>
#include <array>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <cerrno>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "generator/generator.hpp"
#include "protocol/encode.hpp"
#include "protocol/decode.hpp"

class GeneratorTest : public ::testing::Test
{
protected:
    static inline std::atomic<uint16_t> port_{0};
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
                                         int fd = socket(AF_INET, SOCK_DGRAM, 0);
                                         if (fd < 0)
                                             return;

                                         sockaddr_in addr{};
                                         addr.sin_family = AF_INET;
                                         addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                                         addr.sin_port = htons(0);

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
                                         port_.store(ntohs(addr.sin_port), std::memory_order_release);

                                         timeval tv{};
                                         tv.tv_sec = 0;
                                         tv.tv_usec = 200000; // 200ms poll timeout
                                         setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

                                         ready_.store(true, std::memory_order_release);

                                         while (!stop_.load(std::memory_order_acquire))
                                         {
                                            std::array<uint8_t, 2048> buff{};
                                            ssize_t n = recv(fd, buff.data(), buff.size(), 0);
                                            if (n < 0)
                                            {
                                                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                                                {
                                                    continue;
                                                }
                                                server_ok_.store(false);
                                                break;
                                            }

                                            if (n != static_cast<ssize_t>(protocol::kWireSize))
                                            {
                                                if (stop_.load(std::memory_order_acquire))
                                                {
                                                    continue;
                                                }
                                                server_ok_.store(false);
                                                continue;
                                            }

                                            protocol::MarketDataMsg msg{};
                                            if (protocol::decode(buff.data(), static_cast<size_t>(n), msg))
                                            {
                                                std::lock_guard<std::mutex> lock(mutex_);
                                                received.push_back(msg);
                                            }
                                            else
                                            {
                                                server_ok_.store(false);
                                            }
                                         }
                                         close(fd); });

        while (!ready_.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        received.clear();
    }

    void SetUp() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        received.clear();
        server_ok_.store(true, std::memory_order_release);
    }

    static void TearDownTestSuite()
    {
        stop_.store(true, std::memory_order_release);

        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0)
        {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(port_.load(std::memory_order_acquire));
            protocol::MarketDataMsg wake{0, 1, 0, 0, 0, 0, 0};
            const auto wake_bytes = protocol::encode(wake);
            sendto(fd, wake_bytes.data(), wake_bytes.size(), 0,
                   reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
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

    for (int i = 0; i < 100; ++i)
    {
        size_t current = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            current = received.size();
        }
        if (current >= expected.size())
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::vector<protocol::MarketDataMsg> got;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        got = received;
    }

    ASSERT_EQ(got.size(), expected.size());
    for (size_t i = 0; i < got.size(); ++i)
    {
        EXPECT_EQ(got[i].symbol_id, expected[i].symbol_id);
        EXPECT_EQ(got[i].exchange_ts, expected[i].exchange_ts);
        EXPECT_EQ(got[i].bid_price, expected[i].bid_price);
        EXPECT_EQ(got[i].ask_price, expected[i].ask_price);
        EXPECT_EQ(got[i].bid_size, expected[i].bid_size);
        EXPECT_EQ(got[i].ask_size, expected[i].ask_size);
    }
    EXPECT_TRUE(server_ok_.load(std::memory_order_acquire));
}

TEST_F(GeneratorTest, RunWithRateZero)
{
    generator::Generator gen;
    bool connected = gen.connect("127.0.0.1", port_.load());
    EXPECT_TRUE(connected);
    bool ran = gen.run(10, 0, 1);
    EXPECT_TRUE(ran);

    for (int i = 0; i < 100; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    size_t got = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        got = received.size();
    }
    ASSERT_EQ(got, 10);
    EXPECT_TRUE(server_ok_.load(std::memory_order_acquire));
}

TEST_F(GeneratorTest, RunWithRateLargerThanZero)
{
    generator::Generator gen;
    bool connected = gen.connect("127.0.0.1", port_.load());
    EXPECT_TRUE(connected);
    bool ran = gen.run(10, 1000, 1);
    EXPECT_TRUE(ran);

    for (int i = 0; i < 100; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    size_t got = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        got = received.size();
    }
    ASSERT_EQ(got, 10);
    EXPECT_TRUE(server_ok_.load(std::memory_order_acquire));
}
