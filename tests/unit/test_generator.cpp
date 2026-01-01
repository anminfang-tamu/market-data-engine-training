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

                                         if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)), 0)
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
                                             if (cfd >= 0)
                                             {
                                                 close(cfd);
                                             }
                                         }

                                         close(fd); });

        while (!ready_.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
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
    generator::Generator g;

    bool connected = g.connect("127.0.0.1", port_.load());
    EXPECT_TRUE(connected);
}