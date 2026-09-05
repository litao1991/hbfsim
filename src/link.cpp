#include "hbfsim/core.h"

#include <algorithm>
#include <cmath>

namespace hbfsim {

SimTime LinkResource::reserve(SimTime now, std::uint64_t bytes) {
  const SimTime start = std::max(now, free_at_);
  const auto transfer = static_cast<SimTime>(std::ceil(static_cast<double>(bytes) / bytes_per_ns_));
  free_at_ = start + transfer;
  return free_at_ + fixed_latency_ns_;
}

DataFabric::DataFabric(std::uint32_t ports, double aggregate_bytes_per_ns, SimTime fixed_latency_ns)
    : port_free_at_(ports, 0),
      aggregate_bytes_per_ns_(aggregate_bytes_per_ns),
      port_bytes_per_ns_(aggregate_bytes_per_ns / ports),
      fixed_latency_ns_(fixed_latency_ns) {}

SimTime DataFabric::reserve(SimTime now, std::uint64_t bytes, std::uint32_t port) {
  const auto port_index = port % port_free_at_.size();
  const SimTime start = std::max({now, aggregate_free_at_, port_free_at_.at(port_index)});
  const auto aggregate_time = static_cast<SimTime>(std::ceil(static_cast<double>(bytes) / aggregate_bytes_per_ns_));
  const auto port_time = static_cast<SimTime>(std::ceil(static_cast<double>(bytes) / port_bytes_per_ns_));
  aggregate_free_at_ = start + aggregate_time;
  port_free_at_.at(port_index) = start + port_time;
  return std::max(aggregate_free_at_, port_free_at_.at(port_index)) + fixed_latency_ns_;
}

}  // namespace hbfsim
