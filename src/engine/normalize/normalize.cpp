#include "engine/normalize/normalize.hpp"

namespace engine
{
    bool isSizeMatch(size_t len)
    {
        return len == protocol::kWireSize;
    }

    bool sanityCheck(const protocol::MarketDataMsg &msg)
    {
        return msg.bid_price >= 0 && msg.ask_price && msg.bid_price <= msg.ask_price && msg.bid_size > 0 && msg.ask_size > 0;
    }

    bool decode_msg(const void *data, size_t len, protocol::MarketDataMsg &out)
    {
        if (!protocol::decode(static_cast<const uint8_t *>(data), len, out))
        {
            return false;
        }

        return true;
    }
}