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
        running_ = false;
        received_ = 0;
        decoded_err_ = 0;
        processed_ = 0;
        drops_ = 0;
    }

    void Engine::on_message(const void *data, size_t len)
    {
        if (!engine::isSizeMatch(len))
        {
            std::cout << "Message size is not matched!" << std::endl;
            ++drops_;
            return;
        }

        protocol::MarketDataMsg msg{};
        if (!engine::decode_msg(data, len, msg))
        {
            std::cout << "Failed to decode incoming message!" << std::endl;
            ++decoded_err_;
            return;
        }

        if (!engine::sanityCheck(msg))
        {
            std::cout << "Failed to pass sanity check!" << std::endl;
            ++drops_;
            return;
        }

        if (!queue_.enqueue(msg))
        {
            std::cout << "Waiting to enqueue!" << std::endl;
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
            }
            else if (queue_.closed())
            {
                break;
            }
        }
    }
}
