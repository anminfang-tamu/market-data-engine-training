#include <unistd.h>
#include <chrono>
#include <thread>

#include "generator/generator.hpp"

namespace generator
{
    Generator::~Generator()
    {
        net::udp::close_client(client_);
    }

    uint32_t Generator::next_rand()
    {
        // Classic LCG (Numerical Recipes parameters): state = state * a + c.
        rng_state_ = rng_state_ * 1664525u + 1013904223u;
        return rng_state_;
    }

    bool Generator::connect(const char *addr, uint16_t port)
    {
        return net::udp::open_client(client_, addr, port);
    }

    bool Generator::send(const protocol::MarketDataMsg &msg)
    {
        if (!client_.connected || client_.fd < 0)
        {
            return false;
        }
        return net::udp::send_message(client_, msg);
    }

    bool Generator::run(int count, int rate, int seed, int gap_mod, int gap_span)
    {
        if (!client_.connected || client_.fd < 0)
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
        const bool inject_gap = gap_mod > 0 && gap_span > 0;

        // no rate: as fast as possible
        if (rate <= 0)
        {
            for (int i = 0; i < count; i++)
            {
            if (inject_gap)
            {
                uint32_t r = next_rand();
                if ((r % gap_mod) < static_cast<uint32_t>(gap_span))
                {
                    // Skip gap_span sequence numbers, so jump past them before sending next.
                    seq_ += static_cast<uint64_t>(gap_span) + 1;
                    continue; // skip these seq numbers
                }
            }

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
            if (inject_gap)
            {
                uint32_t r = next_rand();
                if ((r % gap_mod) < static_cast<uint32_t>(gap_span))
                {
                    // Skip gap_span sequence numbers; jump ahead so the next sent seq reflects the gap.
                    seq_ += static_cast<uint64_t>(gap_span) + 1;
                    next += interval;
                    std::this_thread::sleep_until(next);
                    continue; // gap
                }
            }

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
        const uint32_t r = next_rand();
        uint32_t sym = (symbol_count_ == 0) ? 1u : (r % symbol_count_) + 1u;
        const uint64_t seq = seq_++;
        int64_t ts = static_cast<int64_t>(seq);
        /*
            1. Calculate the range size: (Max - Min) + 1 => (1000 - 10) + 1 = 991
            2. The price range is from 10 to 1000
        */
        int64_t bid = 10 + static_cast<int64_t>(r % 991);
        int64_t ask = bid + 1;
        int32_t size = 1u + (r % 100);
        return {seq, sym, ts, bid, ask, size, size + 1};
    }

}
