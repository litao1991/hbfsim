#include "hbfsim/core.h"

#include "../test_support.h"

namespace {

hbfsim::SimTime bank_run(std::uint32_t banks) {
  using namespace hbfsim;
  auto config = Config::for_profile(SimulationProfile::HbfV07);
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.banks_per_die = banks;
  config.blocks_per_plane = 1;
  config.pages_per_block = 2;
  config.host_channels_per_stack = 1;
  config.hbf_channel_count = 1;
  config.ports_per_stack = 2;
  config.max_active_planes_per_die = 2;
  config.max_active_planes_per_stack = 2;
  config.initialization_mode = InitializationMode::ImageLoaded;
  config.read_cache_enabled = false;
  config.t_ccs_ns = 500;
  config.read_ns = 1000;
  config.host_fixed_latency_ns = 0;
  config.internal_fixed_latency_ns = 0;
  config.host_bw_bytes_per_ns = 1000;
  config.internal_bw_bytes_per_ns = 1000;
  config.internal_port_bw_bytes_per_ns = 1000;
  Simulator simulator(config);
  simulator.submit({0, OpType::Read, 0, 64, 0});
  simulator.submit({0, OpType::Read, 4096, 64, 0});
  simulator.run();
  return simulator.now();
}

}  // namespace

int main() {
  using namespace hbfsim;
  auto config = Config::for_profile(SimulationProfile::HbfV07);
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.banks_per_die = 1;
  config.blocks_per_plane = 2;
  config.pages_per_block = 4;
  config.host_channels_per_stack = 1;
  config.hbf_channel_count = 1;
  config.ports_per_stack = 2;
  config.max_active_planes_per_die = 2;
  config.max_active_planes_per_stack = 2;
  config.initialization_mode = InitializationMode::ImageLoaded;
  config.strict_media_validation = true;
  config.read_ns = 1000;
  config.host_fixed_latency_ns = 0;
  config.internal_fixed_latency_ns = 0;
  config.host_bw_bytes_per_ns = 1000;
  config.internal_bw_bytes_per_ns = 1000;
  config.internal_port_bw_bytes_per_ns = 1000;
  config.validate();

  Simulator simulator(config);
  simulator.submit({0, OpType::Read, 0, 64, 0});
  simulator.run();
  const auto first_latency = simulator.responses().back().completion_time;
  CHECK(simulator.stats().read_cache_misses() == 1);
  CHECK(simulator.stats().read_cache_hits() == 0);

  const auto second_start = simulator.now();
  simulator.submit({second_start, OpType::Read, 0, 64, 0});
  simulator.run();
  const auto second_latency =
      simulator.responses().back().completion_time - second_start;
  CHECK(simulator.stats().read_cache_hits() == 1);
  CHECK(second_latency < first_latency);

  simulator.submit({simulator.now(), OpType::Read, 4096, 64, 0});
  simulator.run();
  simulator.submit({simulator.now(), OpType::Read, 8192, 64, 0});
  simulator.run();
  CHECK(simulator.stats().read_cache_misses() == 3);
  CHECK(simulator.stats().read_cache_evictions() == 1);
  simulator.submit({simulator.now(), OpType::Read, 0, 64, 0});
  simulator.run();
  CHECK(simulator.stats().read_cache_misses() == 4);
  CHECK(bank_run(2) < bank_run(1));
  return 0;
}
