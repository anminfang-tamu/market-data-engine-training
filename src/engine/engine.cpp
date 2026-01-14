#include "engine.hpp"
#include "common/net/tcp_server_block.hpp"
#include "protocol/md_message.hpp"
#include "protocol/decode.hpp"
#include "engine/normalize/normalize.hpp"
#include "common/logging/logger.hpp"

#include <chrono>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace engine
{
    Engine::~Engine()
    {
        stop();
    }

    void Engine::on_message(const void *data, size_t len)
    {
        if (!engine::isSizeMatch(len))
        {
            m_.inc_drops();
            return;
        }

        protocol::MarketDataMsg msg{};
        if (!engine::decode_msg(data, len, msg))
        {
            m_.inc_decode_error();
            return;
        }

        if (!engine::sanityCheck(msg))
        {
            m_.inc_drops();
            return;
        }

        if (!queue_.enqueue(msg))
        {
            return;
        }

        m_.inc_received();
    }

    bool Engine::run()
    {
        int port = 8888;
        listen_fd_ = net::make_listen_socket(port);
        if (listen_fd_ < 0)
        {
            LOG_ERROR("Failed to activate server on port ", port);
            return false;
        }
        LOG_INFO("Listening on port: ", port);

        running_.store(true, std::memory_order_relaxed);

        processor_ = std::thread([this]
                                 { process_loop(); });

        reporter_ = std::thread([this]
                                {
            while (running_.load(std::memory_order_relaxed)) 
            {
                auto snap = m_.snapshot();
                LOG_INFO("Metrics ", m_.to_string(snap),
                                                 " QueueSize=", queue_.size());
                                        std::this_thread::sleep_for(std::chrono::seconds(5));
            } });

        while (running_.load(std::memory_order_relaxed))
        {
            int cfd = net::accept_one(listen_fd_);
            client_fd_.store(cfd, std::memory_order_relaxed);

            if (!running_.load(std::memory_order_relaxed) || listen_fd_ < 0)
            {
                break;
            }

            if (cfd < 0)
            {
                continue;
            }

            // keep blocking sockets but add a timeout so stop() can tear down cleanly
            net::set_recv_timeout(cfd, std::chrono::milliseconds(500));

            net::handle_client(cfd, protocol::kWireSize, [this](const void *data, size_t len)
                               { on_message(data, len); }, [this]()
                               { return !running_.load(std::memory_order_relaxed); });

            close(cfd);
            client_fd_.store(-1, std::memory_order_relaxed);
        }

        return true;
    }

    bool Engine::stop()
    {
        running_.store(false, std::memory_order_relaxed);
        if (listen_fd_ >= 0)
        {
            shutdown(listen_fd_, SHUT_RDWR); // immediately shutdown
            close(listen_fd_);               // might hang out seconds
            listen_fd_ = -1;
        }

        int cfd = client_fd_.exchange(-1, std::memory_order_relaxed);
        if (cfd >= 0)
        {
            shutdown(cfd, SHUT_RDWR);
            close(cfd);
        }

        queue_.close();
        if (processor_.joinable())
        {
            processor_.join();
        }
        if (reporter_.joinable())
        {
            reporter_.join();
        }
        return true;
    }

    void Engine::process_loop()
    {
        protocol::MarketDataMsg msg{};
        while (true)
        {
            if (queue_.dequeue(msg))
            {
                m_.inc_processed();
            }
            else if (queue_.closed())
            {
                break;
            }
        }
    }
}
