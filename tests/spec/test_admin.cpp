#include "hbfsim/core.h"

#include "../test_support.h"

namespace {

hbfsim::Config config() {
  using namespace hbfsim;
  auto value = Config::for_profile(SimulationProfile::HbfV07);
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 2;
  value.banks_per_die = 1;
  value.blocks_per_plane = 2;
  value.pages_per_block = 4;
  value.host_channels_per_stack = 1;
  value.hbf_channel_count = 1;
  value.ports_per_stack = 2;
  value.max_active_planes_per_die = 2;
  value.max_active_planes_per_stack = 2;
  value.spec_zones_per_channel = 2;
  value.spec_zone_size_pages = 8;
  value.host_fixed_latency_ns = 0;
  value.internal_fixed_latency_ns = 0;
  value.host_bw_bytes_per_ns = 1000;
  value.internal_bw_bytes_per_ns = 1000;
  value.internal_port_bw_bytes_per_ns = 1000;
  value.validate();
  return value;
}

}  // namespace

int main() {
  using namespace hbfsim;
  Simulator simulator(config());
  CHECK(simulator.read_register(0, hbf_register::kBucCap).ok());
  CHECK(simulator.read_register(0, hbf_register::kVersion).value ==
        (7ULL << 16));
  CHECK(simulator.read_register(0, hbf_register::kBucc).value & 1U);
  CHECK(simulator.read_register(0, hbf_register::kMaxPec).value == 0);
  CHECK(simulator.read_register(0, hbf_register::kAvgPec).value == 0);
  CHECK(simulator.read_register(0, hbf_register::kReducedCapacity).value == 0);

  const auto before = simulator.system().mapper().map_channel_read({0, 0, 0, 0});
  HbfAdminCommand swap;
  swap.channel = 0;
  swap.opcode = HbfAdminOpcode::ZoneRemapping;
  swap.zone_swaps = {{0, 1}};
  CHECK(simulator.submit_admin(swap).ok());
  const auto after = simulator.system().mapper().map_channel_read({0, 0, 0, 0});
  CHECK(before.block != after.block);
  CHECK(simulator.system().spec_zones().physical_zone(0, 0) == 1);

  HbfAdminCommand duplicate = swap;
  duplicate.zone_swaps = {{0, 1}, {1, 0}};
  CHECK(simulator.submit_admin(duplicate).status == HbfStatus::InvalidUserField);
  simulator.submit({0, OpType::Read, 0, 64, 0});
  CHECK(simulator.submit_admin(swap).status == HbfStatus::TemporarilyRestricted);
  return 0;
}
