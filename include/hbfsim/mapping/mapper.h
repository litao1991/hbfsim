#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/mapping/stripe_mapping.h"
#include "hbfsim/media/topology.h"
#include "hbfsim/protocol/channel.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace hbfsim {

class AddressMapper {
 public:
  explicit AddressMapper(const Config& config);
  AddressMapper(const Config& config, const HbfChannelDomain& channels);
  AddressMapper(const Config& config, const HbfChannelDomain& channels,
                const NandTopology& topology);
  PhysicalAddr placement(std::uint64_t lpn) const;
  PhysicalAddr preview_write(std::uint64_t lpn) const;
  PhysicalAddr map_read(std::uint64_t lpn) const;
  PhysicalAddr prepare_write(std::uint64_t lpn);
  PhysicalAddr map_channel_read(const HbfChannelAddress& address) const;
  PhysicalAddr prepare_channel_write(const HbfChannelAddress& address);
  void commit_write(std::uint64_t lpn, const PhysicalAddr& paddr,
                    SimTime now = 0);
  ProgramFailureNotice fail_write(std::uint64_t lpn,
                                  const PhysicalAddr& paddr);
  std::optional<PhysicalAddr> lookup(std::uint64_t lpn) const;
  void on_erase(const PhysicalAddr& block_addr);
  std::uint32_t flat_plane(const PhysicalAddr& addr) const;
  StripeMappingTable* stripe_mapping() { return stripes_.get(); }
  const StripeMappingTable* stripe_mapping() const { return stripes_.get(); }
  IMediaManagementMapping* media_management_mapping() { return stripes_.get(); }
  const IMediaManagementMapping* media_management_mapping() const {
    return stripes_.get();
  }
  bool validate_generation(const PhysicalAddr& paddr) const;
  void set_zone_resolver(std::uint32_t zone_count,
                         std::function<std::uint32_t(std::uint64_t)> resolver) {
    if (stripes_) stripes_->set_zone_resolver(zone_count, std::move(resolver));
  }
  const HbfChannelDomain& channels() const { return *channels_; }
  const NandTopology& topology() const { return *topology_; }

 private:
  PhysicalAddr base_map(std::uint64_t lpn) const;
  PhysicalAddr base_map_channel(const HbfChannelAddress& address) const;
  const Config& config_;
  std::unique_ptr<HbfChannelDomain> owned_channels_;
  const HbfChannelDomain* channels_ = nullptr;
  std::unique_ptr<NandTopology> owned_topology_;
  const NandTopology* topology_ = nullptr;
  std::unique_ptr<StripeMappingTable> stripes_;
};

}  // namespace hbfsim
