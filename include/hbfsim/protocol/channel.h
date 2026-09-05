#pragma once

#include "hbfsim/config/config.h"
#include <cstdint>

namespace hbfsim {

struct HbfChannelAddress {
  std::uint32_t channel = 0;
  std::uint64_t local_address = 0;
  std::uint32_t axi_port = 0;
  std::uint64_t axi_port_local_address = 0;
};

class HbfChannelDomain {
 public:
  explicit HbfChannelDomain(const Config& config);
  HbfChannelAddress translate(std::uint64_t global_address) const;
  std::uint64_t global_address(const HbfChannelAddress& address) const;
  std::uint32_t channel_count() const { return channel_count_; }
  std::uint64_t channel_capacity() const { return channel_capacity_; }
  std::uint64_t total_capacity() const { return total_capacity_; }
  std::uint64_t interleave() const { return interleave_; }
  std::uint32_t axi_ports_per_channel() const {
    return axi_ports_per_channel_;
  }
  std::uint64_t axi_port_interleave() const {
    return axi_port_interleave_;
  }

 private:
  std::uint32_t channel_count_ = 0;
  std::uint64_t channel_capacity_ = 0;
  std::uint64_t total_capacity_ = 0;
  std::uint64_t interleave_ = 0;
  std::uint32_t axi_ports_per_channel_ = 0;
  std::uint64_t axi_port_interleave_ = 0;
};

}  // namespace hbfsim
