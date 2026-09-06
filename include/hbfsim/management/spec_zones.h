#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/protocol/channel.h"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hbfsim {

// Per-UCIe-Channel L2P Zone mapping defined by HBF v0.7 §5.2.4.6. It owns
// placement only; the Host remains responsible for data rewrite after swap.
class SpecZoneManager {
 public:
  SpecZoneManager(const Config& config, const HbfChannelDomain& channels);

  bool enabled() const { return !logical_to_physical_.empty(); }
  std::uint32_t zones_per_channel() const { return zones_per_channel_; }
  std::uint64_t physical_local_page(std::uint32_t channel,
                                    std::uint64_t local_page) const;
  void swap(std::uint32_t channel, std::uint32_t source,
            std::uint32_t destination);
  std::uint32_t physical_zone(std::uint32_t channel,
                              std::uint32_t logical_zone) const;

 private:
  const Config& config_;
  const HbfChannelDomain& channels_;
  std::uint32_t zones_per_channel_ = 0;
  std::uint64_t zone_size_pages_ = 0;
  std::vector<std::vector<std::uint32_t>> logical_to_physical_;
};

}  // namespace hbfsim
