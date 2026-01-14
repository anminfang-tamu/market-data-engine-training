#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace metrics
{
    struct Snapshot
    {
        uint64_t processed{0};
        uint64_t received{0};
        uint64_t decode_errors{0};
        uint64_t drops{0};
    };

    class Metrics
    {
    public:
        void inc_processed(uint64_t n = 1);
        void inc_received(uint64_t n = 1);
        void inc_decode_error(uint64_t n = 1);
        void inc_drops(uint64_t n = 1);

        Snapshot snapshot() const;
        std::string to_string(const Snapshot &s) const;

    private:
        std::atomic<uint64_t> processed_{0};
        std::atomic<uint64_t> received_{0};
        std::atomic<uint64_t> decode_errors_{0};
        std::atomic<uint64_t> drops_{0};
    };
}
