#include "common/metrics/metrics.hpp"

#include <sstream>

namespace metrics {
void Metrics::inc_processed(uint64_t n) {
  processed_.fetch_add(n, std::memory_order_relaxed);
}

void Metrics::inc_received(uint64_t n) {
  received_.fetch_add(n, std::memory_order_relaxed);
}

void Metrics::inc_decode_error(uint64_t n) {
  decode_errors_.fetch_add(n, std::memory_order_relaxed);
}

void Metrics::inc_drops(uint64_t n) {
  drops_.fetch_add(n, std::memory_order_relaxed);
}

void Metrics::inc_seq_num_gaps(uint64_t n) {
  seq_num_gaps_.fetch_add(n, std::memory_order_relaxed);
}

Snapshot Metrics::snapshot() const {
  Snapshot s;
  s.processed = processed_.load(std::memory_order_relaxed);
  s.received = received_.load(std::memory_order_relaxed);
  s.decode_errors = decode_errors_.load(std::memory_order_relaxed);
  s.drops = drops_.load(std::memory_order_relaxed);
  s.seq_num_gaps = seq_num_gaps_.load(std::memory_order_relaxed);
  return s;
}

std::string Metrics::to_string(const Snapshot &s) const {
  std::ostringstream oss;
  oss << "processed=" << s.processed << " received=" << s.received
      << " decode_errors=" << s.decode_errors << " drops=" << s.drops
      << " seq_gaps=" << s.seq_num_gaps;
  return oss.str();
}
} // namespace metrics
