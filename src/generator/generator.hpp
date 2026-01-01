#pragma once
#include <cstdint>

#include "protocol/md_message.hpp"

namespace generator
{
    class Generator
    {
    public:
        Generator() = default;
        ~Generator();

        bool connect(const char *addr, uint16_t port);
        bool send(const protocol::MarketDataMsg &msg);
        bool run(int count, int rate, int seed);

    private:
        protocol::MarketDataMsg make_message();

        int socket_fd{-1};
        bool connected{false};
        uint32_t symbol_count_{1};
        uint32_t rng_state_{1}; // random number generator(RNG)
        uint64_t seq_{0};
    };
}
