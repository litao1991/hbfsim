#include "hbfsim/core.h"

#include "../test_support.h"

namespace {

hbfsim::Config small_config() {
  hbfsim::Config config;
  config.simulation_profile = hbfsim::SimulationProfile::HbfV07;
  config.research_stripe_mapping_enabled = false;
  config.research_copy_gc_enabled = false;
  config.research_migration_recovery_enabled = false;
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 1;
  config.blocks_per_plane = 2;
  config.pages_per_block = 4;
  config.host_channels_per_stack = 1;
  config.ports_per_stack = 1;
  config.max_active_planes_per_die = 1;
  config.max_active_planes_per_stack = 1;
  config.mapping_policy = hbfsim::MappingPolicy::Linear;
  return config;
}

}  // namespace

int main() {
  using namespace hbfsim;
  const auto config = small_config();
  config.validate();

  HbfSystem system(config);
  CHECK(system.profile() == SimulationProfile::HbfV07);
  CHECK(system.protocol_abstraction() == ProtocolAbstraction::Transaction);
  CHECK(system.capabilities().spec_profile);
  CHECK(!system.capabilities().ai_system_semantics);
  CHECK(!system.capabilities().research_stripe_mapping);
  CHECK(system.mapper().stripe_mapping() == nullptr);
  CHECK(&system.mapper() == &system.mapper());
  CHECK(&system.host_router() == &system.host_router());
  CHECK(&system.reliability() == &system.reliability());
  CHECK(&system.host_gc_manager() == &system.host_gc_manager());
  CHECK(&system.refresh_manager() == &system.refresh_manager());

  Simulator simulator(config);
  CHECK(simulator.system().profile() == SimulationProfile::HbfV07);
  simulator.submit({0, OpType::Write, 0, config.page_size, 0});
  simulator.run();
  CHECK(simulator.stats().completed_requests() == 1);
  return 0;
}
