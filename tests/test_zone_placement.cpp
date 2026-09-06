#include "hbfsim/core.h"

#include <cstdlib>
#include <iostream>

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':' \
                << __LINE__ << '\n';                                       \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (false)

int main() {
  hbfsim::Config config;
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 1;
  config.blocks_per_plane = 4;
  config.pages_per_block = 2;
  config.page_size = 4096;
  config.host_channels_per_stack = 1;
  config.ports_per_stack = 1;
  config.max_active_planes_per_die = 1;
  config.max_active_planes_per_stack = 1;
  config.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  config.zone_count = 2;
  config.zone_size_pages = 2;
  hbfsim::Simulator simulator(config);
  simulator.remap_zone(0, 1);
  const auto paddr = simulator.system().mapper().prepare_write(0);
  CHECK(paddr.physical_stripe >= 2);
  CHECK(simulator.system().zones().physical_zone(0) == 1);
  return EXIT_SUCCESS;
}
