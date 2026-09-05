#include "hbfsim/core.h"

#include <cassert>

int main() {
  hbfsim::Config c;
  c.stacks = 1;
  c.dies_per_stack = 1;
  c.planes_per_die = 2;
  c.blocks_per_plane = 2;
  c.pages_per_block = 4;
  c.page_size = 4096;
  c.max_active_planes_per_die = 2;
  c.max_active_planes_per_stack = 2;
  c.mapping_policy = hbfsim::MappingPolicy::FineStripe;
  c.host_bw_bytes_per_ns = 4096;
  c.internal_bw_bytes_per_ns = 4096;
  c.host_fixed_latency_ns = 0;
  c.internal_fixed_latency_ns = 0;
  c.read_ns = 10;
  c.warmup_requests = 1;

  hbfsim::Simulator sim(c);
  sim.submit({0, hbfsim::OpType::Read, 0, 4096, 0});
  sim.submit({5, hbfsim::OpType::Read, 4096, 4096, 0});
  sim.run();
  assert(sim.stats().completed_requests() == 1);
  assert(sim.stats().mean_latency_ns() > 0.0);
}
