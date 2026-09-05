#pragma once

#include <cstdint>
#include <vector>

namespace hbfsim {

// Host-visible capacity view. A set bit in retired_stripe_bitmap means the
// corresponding physical Stripe can no longer be selected for placement.
struct ReducedCapacityReport {
  std::uint64_t retired_blocks = 0;
  std::uint64_t retired_stripes = 0;
  std::uint64_t usable_physical_capacity_bytes = 0;
  std::uint64_t usable_host_capacity_bytes = 0;
  std::vector<bool> retired_stripe_bitmap;
};

}  // namespace hbfsim
