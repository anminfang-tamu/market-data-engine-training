#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "md_message.hpp"

namespace protocol
{
    // Wire size is explicit to avoid struct padding differences:
    // seq_num(8) + symbol_id(4) + exchange_ts(8) + bid/ask(8+8) + sizes(4+4) = 44.
    constexpr std::size_t kWireSize = 44;

// make sure match hardware arch
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    inline uint32_t host_to_be32(uint32_t v) { return __builtin_bswap32(v); }
    inline uint64_t host_to_be64(uint64_t v) { return __builtin_bswap64(v); }
    inline uint32_t be32_to_host(uint32_t v) { return __builtin_bswap32(v); }
    inline uint64_t be64_to_host(uint64_t v) { return static_cast<uint64_t>(__builtin_bswap64(v)); }
#else
    inline uint32_t host_to_be32(uint32_t v) { return v; }
    inline uint64_t host_to_be64(uint64_t v) { return v; }
    inline uint32_t be32_to_host(uint32_t v) { return v; }
    inline uint64_t be64_to_host(uint64_t v) { return v; }
#endif

    inline void write_u64(std::array<uint8_t, kWireSize> &buff, std::size_t &off, uint64_t v)
    {
        uint64_t be = host_to_be64(v);
        std::memcpy(buff.data() + off, &be, sizeof(be));
        off += sizeof(be);
    }

    // network strict size on uint8_t -> 8 bits
    inline void write_u32(std::array<uint8_t, kWireSize> &buff, std::size_t &off, uint32_t v)
    {
        uint32_t be = host_to_be32(v);
        std::memcpy(buff.data() + off, &be, sizeof(be));
        off += sizeof(be); // jump 4 * 8 bits or 8 * 8 bits
    }

    // network strict size on uint8_t -> 8 bits
    inline void write_i64(std::array<uint8_t, kWireSize> &buff, std::size_t &off, int64_t v)
    {
        uint64_t be = host_to_be64(static_cast<uint64_t>(v));
        std::memcpy(buff.data() + off, &be, sizeof(be));
        off += sizeof(be); // jump 4 * 8 bits or 8 * 8 bits
    }

    inline std::array<uint8_t, kWireSize> encode(const MarketDataMsg &msg)
    {
        // std::array<uint8_t, kWireSize> out{}; -> value init: write each bit to be 0, slow
        // default init, fast -> avoid double writing each bit
        std::array<uint8_t, kWireSize> out;
        std::size_t off = 0;

        write_u64(out, off, msg.seq_num);
        write_u32(out, off, msg.symbol_id);
        write_i64(out, off, msg.exchange_ts);
        write_i64(out, off, msg.bid_price);
        write_i64(out, off, msg.ask_price);
        write_u32(out, off, msg.bid_size);
        write_u32(out, off, msg.ask_size);

        return out;
    }

}
