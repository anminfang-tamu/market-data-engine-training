#pragma once

#include "protocol/decode.hpp"
#include "protocol/md_message.hpp"
namespace engine {
bool isSizeMatch(size_t len);
bool sanityCheck(const protocol::MarketDataMsg &msg);
bool decode_msg(const void *data, size_t len, protocol::MarketDataMsg &out);
} // namespace engine
