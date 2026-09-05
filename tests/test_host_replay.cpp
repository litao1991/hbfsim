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

namespace {

hbfsim::Config config() {
  hbfsim::Config value;
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 1;
  value.blocks_per_plane = 4;
  value.pages_per_block = 2;
  value.page_size = 4096;
  value.host_channels_per_stack = 1;
  value.ports_per_stack = 1;
  value.max_active_planes_per_die = 1;
  value.max_active_planes_per_stack = 1;
  value.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  value.host_gc_overprovisioning_ratio = 0.25;
  value.program_failure_rate = 1.0;
  value.program_failure_budget = 1;
  value.host_fixed_latency_ns = 0;
  value.internal_fixed_latency_ns = 0;
  value.host_bw_bytes_per_ns = 1000;
  value.internal_bw_bytes_per_ns = 1000;
  value.internal_port_bw_bytes_per_ns = 1000;
  return value;
}

}  // namespace

int main() {
  hbfsim::Simulator simulator(config());
  simulator.submit({0, hbfsim::OpType::Write, 0, 4096, 0});
  simulator.run();
  CHECK(simulator.replay_plans().size() == 1);
  const auto source = simulator.replay_plans().front().source_stripe;
  CHECK(simulator.system().mapper().stripe_mapping()->descriptor(source).state ==
        hbfsim::StripeState::Degraded);

  const auto job = simulator.start_host_replay(0);
  CHECK(job == 0);
  simulator.run();
  CHECK(simulator.active_host_replay_jobs() == 0);
  const auto mapped = simulator.system().mapper().lookup(0);
  CHECK(mapped.has_value());
  CHECK(mapped->physical_stripe != source.physical_id);
  CHECK(simulator.stats().source_bytes(hbfsim::TransactionSource::HostReplay,
                                       hbfsim::OpType::Write) == 4096);
  {
    auto retry_config = config();
    retry_config.program_failure_budget = 2;
    hbfsim::Simulator retry(retry_config);
    retry.submit({0, hbfsim::OpType::Write, 0, 4096, 0});
    retry.run();
    retry.start_host_replay(0);
    retry.run();
    CHECK(retry.replay_plans().size() == 2);
    CHECK(retry.active_host_replay_jobs() == 0);
    CHECK(retry.system().mapper().lookup(0) == std::nullopt);
  }
  return EXIT_SUCCESS;
}
