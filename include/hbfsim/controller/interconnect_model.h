#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/controller/interconnect.h"

#include <cstdint>
#include <vector>

namespace hbfsim {

class InterconnectModel {
 public:
  explicit InterconnectModel(const Config& config);
  LinkResource::Reservation reserve_host(const HostRoute& route,
                                         HostLinkDirection direction,
                                         SimTime now,
                                         std::uint64_t bytes);
  LinkResource::Reservation reserve_fabric(const PhysicalAddr& address,
                                           SimTime now,
                                           std::uint64_t bytes);

 private:
  std::vector<HostInterface> host_interfaces_;
  std::vector<DataFabric> fabrics_;
};

}  // namespace hbfsim
