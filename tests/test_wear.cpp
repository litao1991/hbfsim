#include "hbfsim/core.h"

#include "test_support.h"

namespace {

hbfsim::Config config() {
  hbfsim::Config c;
  c.stacks = 1;
  c.dies_per_stack = 1;
  c.planes_per_die = 1;
  c.blocks_per_plane = 3;
  c.pages_per_block = 2;
  c.page_size = 100;
  c.host_channels_per_stack = 1;
  c.ports_per_stack = 1;
  c.max_active_planes_per_die = 1;
  c.max_active_planes_per_stack = 1;
  c.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  c.strict_media_validation = true;
  c.host_bw_bytes_per_ns = 1000;
  c.internal_bw_bytes_per_ns = 1000;
  c.internal_port_bw_bytes_per_ns = 1000;
  c.host_fixed_latency_ns = 0;
  c.internal_fixed_latency_ns = 0;
  c.read_ns = 2;
  c.program_ns = 3;
  c.erase_ns = 4;
  return c;
}

}  // namespace

int main() {
  {
    auto c = config();
    c.max_erase_cycles = 1;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({20, hbfsim::OpType::Erase, 0, 0, 0});
    sim.run();
    CHECK(sim.block_erase_count({0, 0, 0, 0, 0}) == 1);
    CHECK(sim.block_state({0, 0, 0, 0, 0}) == hbfsim::BlockState::Bad);
    CHECK(sim.stats().retired_blocks() == 1);
    CHECK(sim.stats().retired_stripes() == 1);
    CHECK(sim.stats().usable_physical_capacity_bytes() == 400);
  }

  {
    auto c = config();
    c.erase_failure_rate = 1.0;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({20, hbfsim::OpType::Erase, 0, 0, 0});
    sim.run();
    CHECK(sim.stats().erase_failures() == 1);
    CHECK(sim.stats().failed_requests() == 1);
    CHECK(sim.stats().retired_blocks() == 1);
    CHECK(sim.block_state({0, 0, 0, 0, 0}) == hbfsim::BlockState::Bad);
  }

  {
    auto c = config();
    c.mapping_policy = hbfsim::MappingPolicy::Linear;
    c.program_failure_rate_per_erase = 1.0;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({20, hbfsim::OpType::Erase, 0, 0, 0});
    sim.submit({40, hbfsim::OpType::Write, 0, 100, 0});
    sim.run();
    CHECK(sim.stats().program_failures() == 1);
  }

  {
    auto c = config();
    c.mapping_policy = hbfsim::MappingPolicy::Linear;
    c.raw_bit_error_rate_per_erase = 1.0;
    c.ecc_correctable_bits = 0;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({20, hbfsim::OpType::Erase, 0, 0, 0});
    sim.submit({40, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({60, hbfsim::OpType::Read, 0, 100, 0});
    sim.run();
    CHECK(sim.stats().uncorrectable_reads() == 1);
    CHECK(sim.stats().failed_requests() == 1);
  }
}
