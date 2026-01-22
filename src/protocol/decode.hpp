#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "md_message.hpp"
#include "encode.hpp"

namespace protocol
{
    inline uint32_t read_u32(const uint8_t *data, std::size_t &off)
    {
        uint32_t be = 0;
        std::memcpy(&be, data + off, sizeof(be));
        off += sizeof(be);
        return be32_to_host(be);
    }

    inline uint64_t read_u64(const uint8_t *data, std::size_t &off)
    {
        uint64_t be = 0;
        std::memcpy(&be, data + off, sizeof(be));
        off += sizeof(be);
        return be64_to_host(be);
    }

    inline int64_t read_i64(const uint8_t *data, std::size_t &off)
    {
        uint64_t be = 0;
        std::memcpy(&be, data + off, sizeof(be));
        off += sizeof(be);
        return static_cast<int64_t>(be64_to_host(be));
    }

    inline bool decode(const uint8_t *data, std::size_t len, MarketDataMsg &msg)
    {
        if (len < kWireSize)
        {
            return false;
        }
        std::size_t off = 0;
        msg.seq_num = read_u64(data, off);
        msg.symbol_id = read_u32(data, off);
        msg.exchange_ts = read_i64(data, off);
        msg.bid_price = read_i64(data, off);
        msg.ask_price = read_i64(data, off);
        msg.bid_size = read_u32(data, off);
        msg.ask_size = read_u32(data, off);

        return true;
    }
}
