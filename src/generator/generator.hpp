#pragma once
#include <cstdint>

#include "common/net/v1/udp_client.hpp"
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
        bool run(int count, int rate, int seed, int gap_mod = 0, int gap_span = 1);

    private:
        uint32_t next_rand();
        protocol::MarketDataMsg make_message();

        net::udp::Client client_{};
        uint32_t symbol_count_{1};
        uint32_t rng_state_{1}; // random number generator(RNG)
        uint64_t seq_{0};
    };
}
