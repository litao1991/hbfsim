#include "hbfsim/core.h"

#include <cassert>

int main() {
  hbfsim::Config c;
  c.stacks = 1; c.dies_per_stack = 1; c.planes_per_die = 2;
  c.blocks_per_plane = 8; c.pages_per_block = 32; c.page_size = 4096;
  c.max_active_planes_per_die = 2; c.max_active_planes_per_stack = 2;
  c.mapping_policy = hbfsim::MappingPolicy::FineStripe;
  c.read_ns = 1000; c.host_fixed_latency_ns = 0; c.internal_fixed_latency_ns = 0;
  c.host_bw_bytes_per_ns = 4096; c.internal_bw_bytes_per_ns = 4096;
  hbfsim::Simulator sim(c);
  sim.submit({0, hbfsim::OpType::Read, 0, 8192, 0});
  sim.run();
  assert(sim.stats().completed_requests() == 1);
  assert(sim.stats().mean_latency_ns() >= 1000.0);
  assert(sim.stats().mean_latency_ns() < 1100.0);
}
