#include "hbfsim/simulator.h"

#include "../test_support.h"

namespace {

hbfsim::Config spec_batch_config() {
  using namespace hbfsim;
  auto config = Config::for_profile(SimulationProfile::HbfV07);
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.banks_per_die = 2;
  config.blocks_per_plane = 1;
  config.pages_per_block = 8;
  config.page_size = 4 * 1024;
  config.dlu_size = 4 * 1024;
  config.host_channels_per_stack = 1;
  config.hbf_channel_count = 1;
  config.ports_per_stack = 2;
  config.max_active_planes_per_die = 2;
  config.max_active_planes_per_stack = 2;
  config.initialization_mode = InitializationMode::ImageLoaded;
  config.read_cache_enabled = false;
  config.batch_read_enabled = true;
  // This deliberately cannot release a Spec Batch Read.
  config.batch_read_aggregation_window_ns = 100'000;
  config.batch_read_max_pages = 1;
  config.read_ns = 100;
  config.host_fixed_latency_ns = 0;
  config.internal_fixed_latency_ns = 0;
  config.host_bw_bytes_per_ns = 1000;
  config.internal_bw_bytes_per_ns = 1000;
  config.internal_port_bw_bytes_per_ns = 1000;
  config.validate();
  return config;
}

}  // namespace

int main() {
  using namespace hbfsim;
  auto config = spec_batch_config();
  auto no_timer_config = config;
  no_timer_config.batch_read_aggregation_window_ns = 0;
  no_timer_config.validate();
  Simulator simulator(config);

  // The first three batch requests span two Banks, with two pages in one
  // Bank.  The regular read is the Base-die boundary and must release every
  // pending page, rather than waiting for the 100 us research-only timer or
  // splitting the same-Bank pair at research max_pages=1.
  simulator.submit({0, OpType::Read, 0, 64, 0, 0, 0, true});
  simulator.submit({1, OpType::Read, 4 * 1024, 64, 0, 1, 0, true});
  simulator.submit({2, OpType::Read, 8 * 1024, 64, 0, 2, 0, true});
  simulator.submit({3, OpType::Read, 12 * 1024, 64, 0, 3, 0, false});
  simulator.run();

  CHECK(simulator.responses().size() == 4);
  CHECK(simulator.stats().batch_read_emissions() == 2);
  CHECK(simulator.stats().batch_read_pages() == 3);
  CHECK(simulator.now() < config.batch_read_aggregation_window_ns);
  return 0;
}
