#pragma once
#include <cstdint>

namespace protocol {
struct MarketDataMsg {
  uint64_t seq_num;
  uint32_t symbol_id;
  int64_t exchange_ts;
  int64_t bid_price;
  int64_t ask_price;
  int32_t bid_size;
  int32_t ask_size;
};

// In-memory size is 48 bytes because of padding/alignment.
// Wire size is defined separately in encode.hpp (kWireSize = 44); never use
// sizeof(MarketDataMsg) for network I/O.
static_assert(sizeof(MarketDataMsg) == 48,
              "MarketDataMsg size must be 48 bytes!");
} // namespace protocol
