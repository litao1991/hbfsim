#include "hbfsim/core.h"

#include <algorithm>
#include <cmath>

namespace hbfsim {

LinkResource::Reservation LinkResource::reserve_window(SimTime now,
                                                       std::uint64_t bytes) {
  const SimTime start = std::max(now, free_at_);
  const auto transfer = static_cast<SimTime>(std::ceil(static_cast<double>(bytes) / bytes_per_ns_));
  free_at_ = start + transfer;
  return {now, start, free_at_, free_at_ + fixed_latency_ns_};
}

DataFabric::DataFabric(std::uint32_t ports, double aggregate_bytes_per_ns,
                       double port_bytes_per_ns,
                       SimTime fixed_latency_ns)
    : port_free_at_(ports, 0),
      aggregate_bytes_per_ns_(aggregate_bytes_per_ns),
      port_bytes_per_ns_(port_bytes_per_ns),
      fixed_latency_ns_(fixed_latency_ns) {}

LinkResource::Reservation DataFabric::reserve_window(
    SimTime now, std::uint64_t bytes, std::uint32_t port) {
  const auto port_index = port % port_free_at_.size();
  const SimTime start = std::max({now, aggregate_free_at_, port_free_at_.at(port_index)});
  const auto aggregate_time = static_cast<SimTime>(std::ceil(static_cast<double>(bytes) / aggregate_bytes_per_ns_));
  const auto port_time = static_cast<SimTime>(std::ceil(static_cast<double>(bytes) / port_bytes_per_ns_));
  aggregate_free_at_ = start + aggregate_time;
  port_free_at_.at(port_index) = start + port_time;
  const auto transfer_end =
      std::max(aggregate_free_at_, port_free_at_.at(port_index));
  return {now, start, transfer_end, transfer_end + fixed_latency_ns_};
}

HostRoute HostRouter::route(std::uint64_t logical_addr,
                            const PhysicalAddr& media_address) const {
  if (config_.simulation_profile != SimulationProfile::MediaResearch) {
    const auto address = channels_.translate(logical_addr);
    return {address.channel / config_.host_channels_per_stack,
            address.channel % config_.host_channels_per_stack,
            address.channel, address.local_address, address.axi_port,
            address.axi_port_local_address};
  }
  const auto lpn = logical_addr / config_.page_size;
  return {media_address.stack,
          static_cast<std::uint32_t>(
              lpn % config_.host_channels_per_stack),
          media_address.stack * config_.host_channels_per_stack +
              static_cast<std::uint32_t>(
                  lpn % config_.host_channels_per_stack),
          logical_addr};
}

HostInterface::HostInterface(std::uint32_t channels, double bytes_per_ns,
                             SimTime fixed_latency_ns, bool full_duplex)
    : full_duplex_(full_duplex) {
  channels_.reserve(channels);
  for (std::uint32_t i = 0; i < channels; ++i) {
    channels_.push_back({LinkResource(bytes_per_ns, fixed_latency_ns),
                         LinkResource(bytes_per_ns, fixed_latency_ns),
                         LinkResource(bytes_per_ns, fixed_latency_ns),
                         LinkResource(bytes_per_ns, fixed_latency_ns)});
  }
}

LinkResource::Reservation HostInterface::reserve(
    const HostRoute& route, HostLinkDirection direction, SimTime now,
    std::uint64_t bytes) {
  auto& channel = channels_.at(route.channel % channels_.size());
  if (!full_duplex_) return channel.shared.reserve_window(now, bytes);
  switch (direction) {
    case HostLinkDirection::Command:
      return channel.command.reserve_window(now, bytes);
    case HostLinkDirection::HostToDevice:
      return channel.host_to_device.reserve_window(now, bytes);
    case HostLinkDirection::DeviceToHost:
      return channel.device_to_host.reserve_window(now, bytes);
  }
  return channel.shared.reserve_window(now, bytes);
}

}  // namespace hbfsim
