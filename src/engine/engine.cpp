#include "engine.hpp"
#include "common/net/tcp_server_block.hpp"
#include "protocol/md_message.hpp"
#include "protocol/decode.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace engine
{
    Engine::~Engine()
    {
        running_ = false;
        incoming_msg_count_ = 0;
    }

    void Engine::on_message(const void *data, size_t len)
    {
        if (len != protocol::kWireSize)
        {
            return;
        }

        protocol::MarketDataMsg msg{};
        if (!protocol::decode(static_cast<const uint8_t *>(data), len, msg))
        {
            return;
        }

        std::cout << "Receiving an new message" << std::endl;
        std::cout << "Symbol: " << msg.symbol_id << std::endl;
        std::cout << "Bid: " << msg.bid_price << std::endl;
        std::cout << "Ask: " << msg.ask_price << std::endl;
        std::cout << "Bid size: " << msg.bid_size << std::endl;
        std::cout << "Ask size: " << msg.ask_size << std::endl;

        ++incoming_msg_count_;
    }

    bool Engine::run()
    {
        int port = 8888;
        int fd = net::make_listen_socket(port);
        if (fd < 0)
        {
            std::cout << "It fails to activate server" << std::endl;
            return false;
        }
        std::cout << "Listening on port: " << port << std::endl;

        running_ = true;

        while (running_)
        {
            int cfd = net::accept_one(fd);
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
}
