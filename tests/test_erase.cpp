#include "hbfsim/core.h"

#include "test_support.h"

int main() {
  hbfsim::Config c;
  c.stacks = 1; c.dies_per_stack = 1; c.planes_per_die = 1;
  c.blocks_per_plane = 2; c.pages_per_block = 4; c.page_size = 4096;
  c.max_active_planes_per_die = 1; c.max_active_planes_per_stack = 1;
  c.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  c.host_bw_bytes_per_ns = 4096; c.internal_bw_bytes_per_ns = 4096;
  c.host_fixed_latency_ns = 0; c.internal_fixed_latency_ns = 0;
  c.read_ns = 10; c.program_ns = 20; c.erase_ns = 30;
  c.strict_media_validation = true;
  hbfsim::Simulator sim(c);
  sim.submit({0, hbfsim::OpType::Write, 0, 4096, 0});
  sim.submit({5, hbfsim::OpType::Erase, 0, 0, 0});
  sim.submit({10, hbfsim::OpType::Write, 0, 4096, 0});
  sim.submit({100, hbfsim::OpType::Read, 0, 4096, 0});
  sim.run_until(99);
  CHECK(sim.page_state({0, 0, 0, 0, 0}) == hbfsim::PageState::Valid);
  sim.run();
  CHECK(sim.stats().completed_requests() == 4);
  CHECK(sim.now() >= 100);
}
