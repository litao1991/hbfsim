#include "hbfsim/core.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':' \
                << __LINE__ << '\n';                                       \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (false)

namespace {

hbfsim::Config config() {
  hbfsim::Config value;
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 1;
  value.blocks_per_plane = 4;
  value.pages_per_block = 4;
  value.page_size = 4096;
  value.host_channels_per_stack = 1;
  value.ports_per_stack = 1;
  value.max_active_planes_per_die = 1;
  value.max_active_planes_per_stack = 1;
  value.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  value.zone_count = 2;
  value.zone_size_pages = 2;
  return value;
}

}  // namespace

int main() {
  using namespace hbfsim;
  {
    const auto value = config();
    NandTopology topology(value);
    NandMediaSystem media(value, topology);
    PhysicalAddr first{0, 0, 0, 0, 0};
    PhysicalAddr second = first;
    second.page = 1;
    media.begin_program(first);
    media.complete_program(first, std::nullopt, 100);
    media.begin_program(second);
    media.complete_program(second, std::nullopt, 200);
    // The default model is Block-granular: the final successful program time
    // is shared by all Pages without allocating pages_per_block timestamps.
    CHECK(media.block_retention_age(first, 300) == 100);
    CHECK(media.block_retention_age(second, 300) == 100);
  }
  {
    auto value = config();
    ZoneManager zones(value);
    zones.record_user_write(0);
    zones.record_user_write(1);
    PhysicalAddr paddr{0, 0, 0, 0, 0};
    paddr.physical_stripe = 0;
    zones.record_physical_program(paddr);
    zones.record_physical_erase(paddr, 100);
    PhysicalAddr second = paddr;
    second.block = 1;
    second.physical_stripe = 1;
    zones.record_physical_erase(second, 200);
    zones.remap(0, 1);
    CHECK(zones.physical_zone(0) == 1);
    CHECK(zones.physical_zone(2) == 0);
    CHECK(zones.logical_owner(1) == 0);
    CHECK(zones.hottest_logical().logical_zone == 0);
    CHECK(zones.physical_wear(0).max_pec == 200);
    CHECK(zones.physical_wear(0).avg_pec == 150.0);
    CHECK(zones.physical_wear(0).pec_variance() == 5000.0);
    bool rejected = false;
    try {
      static_cast<void>(zones.physical_zone(4));
    } catch (const std::out_of_range&) {
      rejected = true;
    }
    CHECK(rejected);
  }
  return EXIT_SUCCESS;
}
