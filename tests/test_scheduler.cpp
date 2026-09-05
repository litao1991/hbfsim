#include "hbfsim/core.h"

#include "test_support.h"

int main() {
  hbfsim::Config c;
  c.stacks = 1; c.dies_per_stack = 1; c.planes_per_die = 1;
  c.blocks_per_plane = 8; c.pages_per_block = 32; c.page_size = 4096;
  c.max_active_planes_per_die = 1; c.max_active_planes_per_stack = 1;
  c.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  c.write_starvation_ns = 1; c.max_consecutive_reads = 1;
  hbfsim::Simulator sim(c);
  sim.submit({0, hbfsim::OpType::Write, 0, 4096, 0});
  sim.submit({0, hbfsim::OpType::Read, 0, 4096, 0});
  sim.submit({0, hbfsim::OpType::Write, 4096, 4096, 0});
  sim.run();
  CHECK(sim.stats().completed_requests() == 3);
}
