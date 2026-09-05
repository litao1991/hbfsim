#include "hbfsim/core.h"

#include "../test_support.h"

namespace {

hbfsim::Config config() {
  hbfsim::Config value;
  value.simulation_profile = hbfsim::SimulationProfile::HbfV07;
  value.research_stripe_mapping_enabled = false;
  value.research_copy_gc_enabled = false;
  value.research_migration_recovery_enabled = false;
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 1;
  value.blocks_per_plane = 2;
  value.pages_per_block = 4;
  value.page_size = 4096;
  value.host_channels_per_stack = 1;
  value.ports_per_stack = 1;
  value.max_active_planes_per_die = 1;
  value.max_active_planes_per_stack = 1;
  value.mapping_policy = hbfsim::MappingPolicy::Linear;
  value.strict_media_validation = true;
  value.initialization_mode = hbfsim::InitializationMode::Empty;
  value.host_bw_bytes_per_ns = 1000;
  value.internal_bw_bytes_per_ns = 1000;
  value.internal_port_bw_bytes_per_ns = 1000;
  return value;
}

}  // namespace

int main() {
  using namespace hbfsim;
  {
    auto value = config();
    Simulator simulator(value);
    simulator.submit({0, OpType::Read, 0, 4096, 0, 2, 0});
    simulator.run();
    CHECK(simulator.responses().size() == 1);
    CHECK(simulator.responses()[0].status == HbfStatus::ErasedPageRead);
    CHECK(simulator.responses()[0].protocol_status_code == 0x7);
    CHECK(simulator.stats().failed_requests() == 1);
  }

  {
    auto value = config();
    value.strict_media_validation = false;
    value.program_failure_rate = 1.0;
    Simulator simulator(value);
    simulator.submit({0, OpType::Write, 0, 4096, 0, 1, 0});
    simulator.run();
    CHECK(simulator.responses().size() == 1);
    CHECK(simulator.responses()[0].status ==
          HbfStatus::ProgramFailureReplayRequired);
    CHECK(simulator.responses()[0].protocol_status_code == 0x7);
    CHECK(hbf_status_code(OpType::Write,
                          simulator.responses()[0].status) == 0x7);
  }

  {
    auto value = config();
    Simulator simulator(value);
    simulator.submit({0, OpType::Read,
                      simulator.system().channels().total_capacity(),
                      4096, 0, 0, 0});
    CHECK(simulator.responses().size() == 1);
    CHECK(simulator.responses()[0].status == HbfStatus::InvalidAddress);
    CHECK(simulator.responses()[0].protocol_status_code == 0x1);
    CHECK(hbf_status_code(OpType::Read,
                          simulator.responses()[0].status) == 0x1);
  }
  return 0;
}
