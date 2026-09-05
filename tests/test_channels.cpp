#include "hbfsim/core.h"

#include <cassert>

namespace {

hbfsim::SimTime run_with_channels(std::uint32_t channels) {
  hbfsim::Config c;
  c.stacks = 1;
  c.dies_per_stack = 1;
  c.planes_per_die = 2;
  c.blocks_per_plane = 2;
  c.pages_per_block = 4;
  c.page_size = 100;
  c.host_channels_per_stack = channels;
  c.ports_per_stack = 2;
  c.max_active_planes_per_die = 2;
  c.max_active_planes_per_stack = 2;
  c.mapping_policy = hbfsim::MappingPolicy::FineStripe;
  c.host_bw_bytes_per_ns = 1.0;
  c.internal_bw_bytes_per_ns = 1000.0;
  c.host_fixed_latency_ns = 0;
  c.internal_fixed_latency_ns = 0;
  c.program_ns = 1;

  hbfsim::Simulator sim(c);
  sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
  sim.submit({0, hbfsim::OpType::Write, 100, 100, 0});
  sim.run();
  return sim.now();
}

}  // namespace

int main() {
  assert(run_with_channels(2) < run_with_channels(1));
}
