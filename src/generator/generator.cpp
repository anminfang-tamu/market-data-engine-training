#include <unistd.h>
#include <chrono>
#include <thread>

#include "generator/generator.hpp"
#include "common/net/tcp_client_block.hpp"
#include "protocol/encode.hpp"

namespace generator
{
    Generator::~Generator()
    {
        if (socket_fd >= 0)
        {
            close(socket_fd);
        }
    }

    bool Generator::connect(const char *addr, uint16_t port)
    {
        if (socket_fd >= 0)
        {
            close(socket_fd);
        }

        socket_fd = net::connect_to_server(addr, port);
        connected = socket_fd >= 0;
        return connected;
    }

    bool Generator::send(const protocol::MarketDataMsg &msg)
    {
        if (!connected || socket_fd < 0)
        {
            return false;
        }
        auto bytes = protocol::encode(msg);
        bool sent = net::send_all(socket_fd, bytes.data(), bytes.size());

        return sent;
    }

    bool Generator::run(int count, int rate, int seed)
    {
        if (!connected || socket_fd < 0)
        {
            return false;
        }
        if (count <= 0)
        {
            return true;
        }
        if (seed <= 0)
        {
            seed = 1;
        }

        rng_state_ = static_cast<uint32_t>(seed);
        seq_ = 0;

        // no rate: as fast as possible
        if (rate <= 0)
        {
            for (int i = 0; i < count; i++)
            {
                auto msg = make_message();
                if (!send(msg))
                {
                    return false;
                }
            }
            return true;
        }

        using clock = std::chrono::steady_clock;
        auto interval = std::chrono::nanoseconds(1000000000LL / rate);
        auto next = clock::now();

        for (int i = 0; i < count; ++i)
        {
            auto msg = make_message();
            if (!send(msg))
            {
                return false;
            }
            next += interval;
            std::this_thread::sleep_until(next);
        }

        return true;
    }

    protocol::MarketDataMsg Generator::make_message()
    {
        /*
            The constants 1664525 and 1013904223 are the specific multiplier
            ($a$) and increment ($c$) used by the classic Numerical Recipes
            implementation. This ensures a uniform distribution of bits
            over a large period.
        */
        rng_state_ = rng_state_ * 1664525u * 1013904223u;
        uint32_t sym = (symbol_count_ == 0) ? 1u : (rng_state_ % symbol_count_) + 1u;
        int64_t ts = static_cast<int64_t>(seq_);
        /*
            1. Calculate the range size: (Max - Min) + 1 => (1000 - 10) + 1 = 991
            2. The price range is from 10 to 1000
        */
        int64_t bid = 10 + static_cast<int64_t>(rng_state_ % 991);
        int64_t ask = bid + 1;
        uint32_t size = 1u + (rng_state_ % 100);
        ++seq_;
        ++symbol_count_;
        return {sym, ts, bid, ask, size, size + 1};
    }

}