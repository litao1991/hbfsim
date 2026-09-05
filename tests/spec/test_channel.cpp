#include "hbfsim/core.h"

#include "../test_support.h"

#include <stdexcept>

namespace {

hbfsim::Config config() {
  hbfsim::Config value;
  value.simulation_profile = hbfsim::SimulationProfile::HbfV07;
  value.research_stripe_mapping_enabled = false;
  value.research_copy_gc_enabled = false;
  value.research_migration_recovery_enabled = false;
  value.stacks = 2;
  value.dies_per_stack = 1;
  value.planes_per_die = 2;
  value.blocks_per_plane = 2;
  value.pages_per_block = 4;
  value.page_size = 4096;
  value.host_channels_per_stack = 2;
  value.ports_per_stack = 2;
  value.max_active_planes_per_die = 2;
  value.max_active_planes_per_stack = 2;
  value.hbf_channel_count = 4;
  value.axi_ports_per_channel = 2;
  value.mapping_policy = hbfsim::MappingPolicy::Linear;
  return value;
}

}  // namespace

int main() {
  using namespace hbfsim;
  const auto value = config();
  value.validate();
  HbfChannelDomain channels(value);
  CHECK(channels.channel_count() == 4);
  CHECK(channels.interleave() == 4096);
  CHECK(channels.axi_ports_per_channel() == 2);

  CHECK(channels.translate(0).channel == 0);
  CHECK(channels.translate(4096).channel == 1);
  CHECK(channels.translate(8192).channel == 2);
  CHECK(channels.translate(12288).channel == 3);
  const auto next = channels.translate(16384);
  CHECK(next.channel == 0);
  CHECK(next.local_address == 4096);
  CHECK(channels.global_address(next) == 16384);
  const auto second_port = channels.translate(64);
  CHECK(second_port.channel == 0);
  CHECK(second_port.axi_port == 1);
  CHECK(second_port.axi_port_local_address == 0);

  AddressMapper mapper(value);
  const auto channel_zero = mapper.map_channel_read({0, 0});
  const auto channel_three = mapper.map_channel_read({3, 0});
  CHECK(channel_zero.channel == 0);
  CHECK(channel_three.channel == 3);
  CHECK(mapper.flat_plane(channel_zero) != mapper.flat_plane(channel_three));

  HostRouter router(value);
  const auto route = router.route(12288, {1, 0, 0, 0, 0});
  CHECK(route.global_channel == 3);
  CHECK(route.stack == 1);
  CHECK(route.channel == 1);
  CHECK(route.channel_local_address == 0);
  CHECK(route.axi_port == 0);

  bool rejected = false;
  try {
    static_cast<void>(channels.translate(channels.total_capacity()));
  } catch (const std::out_of_range&) {
    rejected = true;
  }
  CHECK(rejected);
  return 0;
}
