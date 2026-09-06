#include "hbfsim/management/spec_zones.h"

#include <numeric>

namespace hbfsim {

SpecZoneManager::SpecZoneManager(const Config& config,
                                 const HbfChannelDomain& channels)
    : config_(config), channels_(channels),
      zones_per_channel_(config.spec_zones_per_channel),
      zone_size_pages_(config.spec_zone_size_pages) {
  if (zones_per_channel_ == 0 && zone_size_pages_ == 0) return;
  if (zones_per_channel_ == 0 || zone_size_pages_ == 0 ||
      static_cast<std::uint64_t>(zones_per_channel_) * zone_size_pages_ !=
          channels_.channel_capacity() / config_.page_size)
    throw std::invalid_argument("SPEC_ZONES_MUST_PARTITION_EACH_CHANNEL");
  logical_to_physical_.resize(channels_.channel_count());
  for (auto& mapping : logical_to_physical_) {
    mapping.resize(zones_per_channel_);
    std::iota(mapping.begin(), mapping.end(), 0);
  }
}

std::uint64_t SpecZoneManager::physical_local_page(
    std::uint32_t channel, std::uint64_t local_page) const {
  if (!enabled()) return local_page;
  if (channel >= logical_to_physical_.size())
    throw std::out_of_range("SPEC_ZONE_CHANNEL_OUT_OF_RANGE");
  const auto logical = local_page / zone_size_pages_;
  if (logical >= zones_per_channel_)
    throw std::out_of_range("SPEC_ZONE_LOCAL_ADDRESS_OUT_OF_RANGE");
  return static_cast<std::uint64_t>(logical_to_physical_.at(channel).at(logical)) *
             zone_size_pages_ +
         local_page % zone_size_pages_;
}

void SpecZoneManager::swap(std::uint32_t channel, std::uint32_t source,
                           std::uint32_t destination) {
  if (!enabled()) throw std::logic_error("SPEC_ZONE_REMAP_NOT_CONFIGURED");
  if (channel >= logical_to_physical_.size() || source >= zones_per_channel_ ||
      destination >= zones_per_channel_ || source == destination)
    throw std::out_of_range("INVALID_SPEC_ZONE_REMAP");
  std::swap(logical_to_physical_.at(channel).at(source),
            logical_to_physical_.at(channel).at(destination));
}

std::uint32_t SpecZoneManager::physical_zone(std::uint32_t channel,
                                              std::uint32_t logical_zone) const {
  return logical_to_physical_.at(channel).at(logical_zone);
}

}  // namespace hbfsim
