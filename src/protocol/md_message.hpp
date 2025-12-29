#pragma once
#include <cstdint>

namespace protocol
{
    struct MarketDataMsg
    {
        uint32_t symbol_id;
        int64_t exchange_ts;
        int64_t bid_price;
        int64_t ask_price;
        uint32_t bid_size;
        uint32_t ask_size;
    };

    static_assert(sizeof(MarketDataMsg) == 40, "MarketDataMsg size changed!");
}