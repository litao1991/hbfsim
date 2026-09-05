#include "hbfsim/simulator.h"

#include "test_support.h"

namespace {

hbfsim::Config retry_config() {
  using namespace hbfsim;
  auto config = Config::for_profile(SimulationProfile::HbfV07);
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 1;
  config.banks_per_die = 1;
  config.blocks_per_plane = 1;
  config.pages_per_block = 2;
  config.host_channels_per_stack = 1;
  config.hbf_channel_count = 1;
  config.ports_per_stack = 1;
  config.max_active_planes_per_die = 1;
  config.max_active_planes_per_stack = 1;
  config.initialization_mode = InitializationMode::ImageLoaded;
  config.read_cache_enabled = false;
  config.host_driven_read_retry = true;
  config.raw_bit_error_rate = 1.0;
  config.retry_ber_multiplier = 0.0;
  config.ecc_correctable_bits = 0;
  config.max_read_retries = 1;
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
  Simulator simulator(retry_config());
  simulator.submit({0, OpType::Read, 0, 64, 0, 0, 0, false, 0});
  simulator.run();
  CHECK(simulator.responses().size() == 1);
  const auto& retry = simulator.responses().front();
  CHECK(retry.status == HbfStatus::UncorrectableEccRetryRequired);
  CHECK(retry.error);
  CHECK(retry.error->retry_stage == 1);

  simulator.submit({simulator.now(), OpType::Read, 0, 64, 0, 0, 0,
                    false, 1});
  simulator.run();
  CHECK(simulator.responses().size() == 2);
  CHECK(simulator.responses().back().ok());
  return 0;
}
