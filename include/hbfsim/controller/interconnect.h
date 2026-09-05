#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/common/types.h"
#include "hbfsim/config/config.h"
#include "hbfsim/protocol/channel.h"
#include "hbfsim/protocol/request.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace hbfsim {

class LinkResource {
 public:
  LinkResource() = default;
  LinkResource(double bytes_per_ns, SimTime fixed_latency_ns)
      : bytes_per_ns_(bytes_per_ns), fixed_latency_ns_(fixed_latency_ns) {}
  struct Reservation {
    SimTime requested_at = 0;
    SimTime start = 0;
    SimTime transfer_end = 0;
    SimTime completion = 0;
  };
  Reservation reserve_window(SimTime now, std::uint64_t bytes);
  SimTime reserve(SimTime now, std::uint64_t bytes) {
    return reserve_window(now, bytes).completion;
  }
  SimTime free_at() const { return free_at_; }

 private:
  SimTime free_at_ = 0;
  double bytes_per_ns_ = 1.0;
  SimTime fixed_latency_ns_ = 0;
};

class DataFabric {
 public:
  DataFabric() = default;
  DataFabric(std::uint32_t ports, double aggregate_bytes_per_ns,
             double port_bytes_per_ns, SimTime fixed_latency_ns);
  LinkResource::Reservation reserve_window(SimTime now, std::uint64_t bytes,
                                            std::uint32_t port);
  SimTime reserve(SimTime now, std::uint64_t bytes, std::uint32_t port) {
    return reserve_window(now, bytes, port).completion;
  }

 private:
  std::vector<SimTime> port_free_at_;
  SimTime aggregate_free_at_ = 0;
  double aggregate_bytes_per_ns_ = 1.0;
  double port_bytes_per_ns_ = 1.0;
  SimTime fixed_latency_ns_ = 0;
};

class HostRouter {
 public:
  explicit HostRouter(const Config& config);
  HostRouter(const Config& config, const HbfChannelDomain& channels);
  HostRoute route(std::uint64_t logical_addr,
                  const PhysicalAddr& media_address) const;
  HbfChannelAddress channel_address(std::uint64_t logical_addr) const {
    return channels_->translate(logical_addr);
  }
  const HbfChannelDomain& channels() const { return *channels_; }

 private:
  const Config& config_;
  std::unique_ptr<HbfChannelDomain> owned_channels_;
  const HbfChannelDomain* channels_ = nullptr;
};

class HostInterface {
 public:
  HostInterface() = default;
  HostInterface(std::uint32_t channels, double bytes_per_ns,
                SimTime fixed_latency_ns, bool full_duplex);
  LinkResource::Reservation reserve(const HostRoute& route,
                                    HostLinkDirection direction,
                                    SimTime now, std::uint64_t bytes);

 private:
  struct Channel {
    LinkResource shared;
    LinkResource command;
    LinkResource host_to_device;
    LinkResource device_to_host;
  };
  bool full_duplex_ = true;
  std::vector<Channel> channels_;
};

}  // namespace hbfsim
