#include "hbfsim/core.h"

#include "test_support.h"

namespace {

hbfsim::Config base_config(std::uint32_t planes = 1) {
  hbfsim::Config c;
  c.stacks = 1;
  c.dies_per_stack = 1;
  c.planes_per_die = planes;
  c.blocks_per_plane = 2;
  c.pages_per_block = 8;
  c.page_size = 100;
  c.host_channels_per_stack = planes;
  c.ports_per_stack = planes;
  c.max_active_planes_per_die = planes;
  c.max_active_planes_per_stack = planes;
  c.mapping_policy = hbfsim::MappingPolicy::FineStripe;
  c.host_bw_bytes_per_ns = 1000.0;
  c.internal_bw_bytes_per_ns = 1000.0;
  c.host_fixed_latency_ns = 0;
  c.internal_fixed_latency_ns = 0;
  c.read_ns = 10;
  c.program_ns = 20;
  c.erase_ns = 200;
  return c;
}

hbfsim::SimTime multi_plane_run(bool enabled) {
  auto c = base_config(2);
  c.t_ccs_ns = 100;
  c.multi_plane_enabled = enabled;
  c.multi_plane_setup_ns = enabled ? 5 : 0;
  c.max_multi_plane_width = 2;
  hbfsim::Simulator sim(c);
  sim.submit({0, hbfsim::OpType::Read, 0, 100, 0});
  sim.submit({0, hbfsim::OpType::Read, 100, 100, 0});
  sim.run();
  return sim.now();
}

hbfsim::SimTime cache_program_run(bool enabled) {
  auto c = base_config();
  c.internal_bw_bytes_per_ns = 1.0;
  c.program_ns = 100;
  c.cache_program_enabled = enabled;
  hbfsim::Simulator sim(c);
  sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
  sim.submit({0, hbfsim::OpType::Write, 100, 100, 0});
  sim.run();
  return sim.now();
}

hbfsim::SimTime extended_timing_run(bool enabled) {
  auto c = base_config();
  if (enabled) {
    c.t_ccs_ns = 20;
    c.t_adl_ns = 7;
    c.t_whr_ns = 5;
  }
  hbfsim::Simulator sim(c);
  sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
  sim.run();
  if (enabled) {
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Valid);
    CHECK(sim.block_ready_at({0, 0, 0, 0, 0}) == sim.now() + 5);
    CHECK(sim.die_ready_at({0, 0, 0, 0, 0}) == sim.now() + 5);
  }
  return sim.now();
}

}  // namespace

int main() {
  {
    auto c = base_config();
    c.mapping_policy = hbfsim::MappingPolicy::HostManaged;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({100, hbfsim::OpType::Write, 0, 100, 0});
    sim.run();
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Invalid);
    CHECK(sim.page_state({0, 0, 0, 0, 1}) ==
           hbfsim::PageState::Valid);
  }

  {
    auto c = base_config();
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.run_until(10);
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Programming);
    sim.run_until(30);
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Valid);
    sim.submit({31, hbfsim::OpType::Read, 0, 100, 0});
    sim.run_until(35);
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Reading);
    sim.run();
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Valid);
  }

  {
    auto c = base_config();
    c.program_failure_rate = 1.0;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.run();
    CHECK(sim.stats().program_failures() == 1);
    CHECK(sim.stats().failed_requests() == 1);
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Failed);
  }

  {
    auto c = base_config();
    c.raw_bit_error_rate = 1.0;
    c.ecc_correctable_bits = 0;
    c.max_read_retries = 1;
    c.retry_ber_multiplier = 0.0;
    c.read_retry_ns = 5;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({100, hbfsim::OpType::Read, 0, 100, 0});
    sim.run();
    CHECK(sim.stats().read_retries() == 1);
    CHECK(sim.stats().uncorrectable_reads() == 0);
    CHECK(sim.stats().failed_requests() == 0);
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Valid);
  }

  {
    auto c = base_config();
    c.raw_bit_error_rate = 1.0;
    c.ecc_correctable_bits = 1000;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({100, hbfsim::OpType::Read, 0, 100, 0});
    sim.run();
    CHECK(sim.stats().corrected_reads() == 1);
    CHECK(sim.stats().failed_requests() == 0);
  }

  {
    auto c = base_config();
    c.raw_bit_error_rate = 1.0;
    c.ecc_correctable_bits = 0;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({100, hbfsim::OpType::Read, 0, 100, 0});
    sim.run();
    CHECK(sim.stats().uncorrectable_reads() == 1);
    CHECK(sim.stats().failed_requests() == 1);
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Failed);
  }

  CHECK(multi_plane_run(true) < multi_plane_run(false));
  CHECK(cache_program_run(true) < cache_program_run(false));
  CHECK(extended_timing_run(true) > extended_timing_run(false));

  {
    auto c = base_config();
    c.mapping_policy = hbfsim::MappingPolicy::HostManaged;
    c.strict_media_validation = true;
    c.cache_program_enabled = true;
    c.program_ns = 100;
    c.erase_ns = 100;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({10, hbfsim::OpType::Erase, 0, 0, 0});
    sim.submit({20, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({300, hbfsim::OpType::Read, 0, 100, 0});
    sim.run();
    CHECK(sim.stats().completed_requests() == 4);
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Valid);
  }

  {
    auto c = base_config();
    c.mapping_policy = hbfsim::MappingPolicy::HostManaged;
    c.strict_media_validation = true;
    c.suspend_resume_enabled = true;
    c.suspend_ns = 1;
    c.resume_ns = 1;
    hbfsim::Simulator sim(c);
    sim.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    sim.submit({50, hbfsim::OpType::Erase, 0, 0, 0});
    sim.submit({60, hbfsim::OpType::Read, 0, 100, 0});
    sim.run();
    CHECK(sim.stats().completed_requests() == 3);
    CHECK(sim.stats().failed_requests() == 0);
    CHECK(sim.block_state({0, 0, 0, 0, 0}) ==
           hbfsim::BlockState::Free);
    CHECK(sim.page_state({0, 0, 0, 0, 0}) ==
           hbfsim::PageState::Erased);
  }
}
