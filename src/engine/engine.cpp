#include "engine.hpp"
#include "common/net/tcp_server_block.hpp"
#include "protocol/md_message.hpp"
#include "protocol/decode.hpp"
#include "engine/normalize/normalize.hpp"

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
            ++drops_;
            return;
        }

        protocol::MarketDataMsg msg{};
        if (!engine::decode_msg(data, len, msg))
        {
            ++decoded_err_;
            return;
        }

        if (!engine::sanityCheck(msg))
        {
            ++drops_;
            return;
        }

        if (!queue_.enqueue(msg))
        {
            return;
        }

        ++received_;
    }

    bool Engine::run()
    {
        int port = 8888;
        listen_fd_ = net::make_listen_socket(port);
        if (listen_fd_ < 0)
        {
            std::cout << "It fails to activate server" << std::endl;
            return false;
        }
        std::cout << "Listening on port: " << port << std::endl;

        running_ = true;

        processor_ = std::thread([this]
                                 { process_loop(); });

        while (running_)
        {
            int cfd = net::accept_one(listen_fd_);

            if (!running_ || listen_fd_ < 0)
            {
                break;
            }

            if (cfd < 0)
            {
                continue;
            }

            net::handle_client(cfd, protocol::kWireSize, [this](const void *data, size_t len)
                               { on_message(data, len); });

            close(cfd);
        }

        return true;
    }

    bool Engine::stop()
    {
        running_ = false;
        if (listen_fd_ >= 0)
        {
            close(listen_fd_);
            listen_fd_ = -1;
        }
        queue_.close();
        if (processor_.joinable())
        {
            processor_.join();
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
                ++processed_;
                if (processed_ % 100000 == 0)
                {
                    std::cout << "Processed: " << processed_ << std::endl;
                    std::cout << "Received: " << received_ << std::endl;
                    std::cout << "Decoded Error: " << decoded_err_ << std::endl;
                    std::cout << "Dropped: " << drops_ << std::endl;
                    std::cout << "Current Queue Size: " << queue_.size() << std::endl;
                }
            }
            else if (queue_.closed())
            {
                break;
            }
        }
    }
}
