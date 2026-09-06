#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/config/config.h"
#include "hbfsim/protocol/channel.h"

#include <cstdint>
#include <vector>

namespace hbfsim {

// R1-R5 projection used by OCP HBF v0.7 §5.7. A simulator plane is one
// replay lane; this preserves the specification's block/page/stripe formula
// even when a product models several planes beneath one Core die.
struct SpecBlockGeometry {
  std::uint32_t r1_core_dies = 0;
  std::uint32_t r2_banks_per_die = 0;
  std::uint32_t r3_pages_per_block = 0;
  std::uint32_t r4_64b_units_per_page = 0;
  std::uint32_t r5_data_stripe_width = 0;
};

struct SpecBlockReplayPlan {
  std::uint64_t id = 0;
  HostRewriteReason reason = HostRewriteReason::ProgramFailure;
  std::uint32_t channel = 0;
  std::uint64_t failed_global_address = 0;
  std::uint64_t page0_global_address = 0;
  std::uint32_t failed_page = 0;
  SpecBlockGeometry geometry;
  std::vector<std::uint64_t> page_global_addresses;
};

class SpecBlockAddressing {
 public:
  SpecBlockAddressing(const Config& config, const HbfChannelDomain& channels);

  const SpecBlockGeometry& geometry() const { return geometry_; }
  SpecBlockReplayPlan plan(std::uint64_t id, std::uint64_t global_address,
                           HostRewriteReason reason) const;

 private:
  const Config& config_;
  const HbfChannelDomain& channels_;
  SpecBlockGeometry geometry_;
};

}  // namespace hbfsim
