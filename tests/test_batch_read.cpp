#include "hbfsim/hbf_system.h"
#include "hbfsim/simulator.h"

#include "test_support.h"

namespace {

hbfsim::Config config_for_batch_read() {
  using namespace hbfsim;
  auto config = Config::for_profile(SimulationProfile::MediaResearch);
  config.mapping_policy = MappingPolicy::Linear;
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.banks_per_die = 2;
  config.blocks_per_plane = 1;
  config.pages_per_block = 4;
  config.host_channels_per_stack = 1;
  config.hbf_channel_count = 1;
  config.ports_per_stack = 2;
  config.max_active_planes_per_die = 2;
  config.max_active_planes_per_stack = 2;
  config.initialization_mode = InitializationMode::ImageLoaded;
  config.read_cache_enabled = false;
  config.batch_read_enabled = true;
  config.batch_read_aggregation_window_ns = 100;
  config.batch_read_max_pages = 2;
  config.read_ns = 1000;
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
  HbfSystem system(config_for_batch_read());
  PhysicalAddr bank_zero{0, 0, 0, 0, 0};
  bank_zero.bank = 0;
  PhysicalAddr bank_one{0, 0, 1, 0, 0};
  bank_one.bank = 1;

  auto& first = system.media().sense_queue(bank_zero);
  first.enqueue(10);
  first.enqueue(11);
  CHECK(first.size() == 2);
  CHECK(first.front() == 10);
  first.pop_front(10);
  CHECK(first.front() == 11);

  auto& second = system.media().sense_queue(bank_one);
  CHECK(second.empty());
  second.enqueue(20);
  CHECK(second.front() == 20);
  CHECK(first.size() == 1);

  Request request;
  request.op = OpType::Read;
  request.read_type = ReadType::Batch;
  SubRequest subrequest;
  subrequest.op = OpType::Read;
  subrequest.read_type = request.read_type;
  CHECK(subrequest.read_type == ReadType::Batch);

  auto batch_config = config_for_batch_read();
  batch_config.banks_per_die = 1;
  batch_config.validate();
  Simulator simulator(batch_config);
  simulator.submit({0, OpType::Read, 0, 64, 0, 0, 0, true});
  simulator.submit({0, OpType::Read, 4096, 64, 0, 0, 0, true});
  simulator.run();
  CHECK(simulator.responses().size() == 2);
  CHECK(simulator.stats().batch_read_emissions() == 1);
  CHECK(simulator.stats().batch_read_pages() == 2);
  CHECK(simulator.now() >= 2100);
  return 0;
}
