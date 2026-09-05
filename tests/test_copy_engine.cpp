#include "hbfsim/core.h"

#include "test_support.h"

namespace {

hbfsim::Config copy_config() {
  hbfsim::Config config;
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.blocks_per_plane = 5;
  config.pages_per_block = 4;
  config.page_size = 100;
  config.host_channels_per_stack = 2;
  config.ports_per_stack = 2;
  config.max_active_planes_per_die = 2;
  config.max_active_planes_per_stack = 2;
  config.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  config.strict_media_validation = true;
  config.host_bw_bytes_per_ns = 1000;
  config.internal_bw_bytes_per_ns = 1000;
  config.internal_port_bw_bytes_per_ns = 1000;
  config.host_fixed_latency_ns = 0;
  config.internal_fixed_latency_ns = 0;
  config.read_ns = 10;
  config.program_ns = 20;
  config.erase_ns = 30;
  return config;
}

void submit_two_pages(hbfsim::Simulator& simulator) {
  simulator.submit({0, hbfsim::OpType::Write, 0, 100, 0});
  simulator.submit({0, hbfsim::OpType::Write, 100, 100, 0});
}

std::pair<hbfsim::SimTime, std::uint64_t> run_full_stripe_gc(
    hbfsim::Config config) {
  hbfsim::Simulator simulator(config);
  const auto stripe_pages = config.planes_per_die * config.pages_per_block;
  for (std::uint32_t slot = 0; slot < stripe_pages; ++slot)
    simulator.submit({0, hbfsim::OpType::Write,
                      static_cast<std::uint64_t>(slot) * config.page_size,
                      config.page_size, 0});
  simulator.run();
  const auto copy_start = simulator.now();
  simulator.start_host_gc(0);
  simulator.run();
  CHECK(simulator.stats().completed_gc_jobs() == 1);
  return {
      simulator.now() - copy_start,
      simulator.stats().copy_buffer_high_watermark(
          hbfsim::TransactionSource::GarbageCollection)};
}

}  // namespace

int main() {
  {
    auto config = copy_config();
    config.auto_recovery_enabled = true;
    config.program_failure_rate = 1.0;
    config.program_failure_budget = 1;
    hbfsim::Simulator simulator(config);
    submit_two_pages(simulator);
    simulator.run();

    CHECK(simulator.active_copy_jobs() == 0);
    CHECK(simulator.stats().program_failures() == 1);
    CHECK(simulator.program_failure_notices().size() == 1);
    CHECK(simulator.stats().remap_commits() == 1);
    CHECK(simulator.stats().completed_recovery_jobs() == 1);
    CHECK(simulator.stats().source_bytes(
              hbfsim::TransactionSource::Recovery,
              hbfsim::OpType::Read) == 100);
    CHECK(simulator.stats().source_bytes(
              hbfsim::TransactionSource::Recovery,
              hbfsim::OpType::Write) == 200);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) ==
          hbfsim::PageState::Erased);
    CHECK(simulator.page_state({0, 0, 0, 1, 0}) ==
          hbfsim::PageState::Valid);
    CHECK(simulator.page_state({0, 0, 1, 1, 0}) ==
          hbfsim::PageState::Valid);
  }

  {
    auto config = copy_config();
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    simulator.submit({0, hbfsim::OpType::Write, 100, 100, 0});
    simulator.submit({0, hbfsim::OpType::Write, 200, 100, 0});
    simulator.run();
    simulator.invalidate_host_page(100);
    simulator.start_host_gc(0);
    simulator.run();

    CHECK(simulator.active_copy_jobs() == 0);
    CHECK(simulator.stats().remap_commits() == 1);
    CHECK(simulator.stats().completed_gc_jobs() == 1);
    CHECK(simulator.stats().source_bytes(
              hbfsim::TransactionSource::GarbageCollection,
              hbfsim::OpType::Read) == 200);
    CHECK(simulator.stats().source_bytes(
              hbfsim::TransactionSource::GarbageCollection,
              hbfsim::OpType::Write) == 200);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) ==
          hbfsim::PageState::Erased);
    CHECK(simulator.page_state({0, 0, 0, 1, 0}) ==
          hbfsim::PageState::Valid);
    CHECK(simulator.page_state({0, 0, 1, 1, 0}) ==
          hbfsim::PageState::Erased);
    CHECK(simulator.page_state({0, 0, 0, 1, 1}) ==
          hbfsim::PageState::Valid);
  }

  {
    auto config = copy_config();
    config.auto_recovery_enabled = true;
    config.program_failure_rate = 1.0;
    config.program_failure_budget = 2;
    config.max_recovery_attempts = 3;
    config.blocks_per_plane = 2;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    simulator.run();

    CHECK(simulator.active_copy_jobs() == 0);
    CHECK(simulator.stats().program_failures() == 2);
    CHECK(simulator.stats().aborted_migrations() == 1);
    CHECK(simulator.stats().remap_commits() == 1);
    CHECK(simulator.page_state({0, 0, 0, 1, 0}) ==
          hbfsim::PageState::Valid);
  }

  {
    auto config = copy_config();
    config.auto_recovery_enabled = true;
    config.program_failure_rate = 1.0;
    config.max_recovery_attempts = 2;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    simulator.run();

    CHECK(simulator.active_copy_jobs() == 0);
    CHECK(simulator.stats().remap_commits() == 0);
    CHECK(simulator.stats().aborted_migrations() == 2);
    CHECK(simulator.stats().completed_recovery_jobs() == 0);
    CHECK(simulator.stats().failed_recovery_jobs() == 1);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) ==
          hbfsim::PageState::Failed);
    CHECK(simulator.page_state({0, 0, 0, 1, 0}) ==
          hbfsim::PageState::Erased);
    CHECK(simulator.page_state({0, 0, 0, 2, 0}) ==
          hbfsim::PageState::Erased);
  }

  {
    auto config = copy_config();
    config.blocks_per_plane = 1;
    config.auto_recovery_enabled = true;
    config.program_failure_rate = 1.0;
    config.program_failure_budget = 1;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Write, 0, 100, 0});
    simulator.run();
    CHECK(simulator.active_copy_jobs() == 0);
    CHECK(simulator.stats().failed_recovery_jobs() == 1);
    CHECK(simulator.stats().remap_commits() == 0);
  }

  {
    auto config = copy_config();
    config.auto_recovery_enabled = true;
    config.program_failure_rate = 1.0;
    config.program_failure_budget = 5;
    config.copy_max_inflight_programs = 4;
    hbfsim::Simulator simulator(config);
    for (std::uint64_t slot = 0; slot < 4; ++slot)
      simulator.submit({0, hbfsim::OpType::Write,
                        slot * config.page_size, config.page_size, 0});
    simulator.run();

    CHECK(simulator.active_copy_jobs() == 0);
    CHECK(simulator.stats().program_failures() == 5);
    CHECK(simulator.stats().aborted_migrations() == 1);
    CHECK(simulator.stats().completed_recovery_jobs() == 1);
    CHECK(simulator.stats().remap_commits() == 1);
  }

  {
    auto serial = copy_config();
    serial.copy_max_inflight_reads = 1;
    serial.copy_max_inflight_programs = 1;
    serial.copy_buffer_size = serial.page_size;
    serial.copy_prefetch_window_pages = 1;
    const auto [serial_time, serial_high_watermark] =
        run_full_stripe_gc(serial);
    CHECK(serial_high_watermark == serial.page_size);

    auto pipelined = copy_config();
    pipelined.copy_max_inflight_reads = 8;
    pipelined.copy_max_inflight_programs = 4;
    pipelined.copy_buffer_size = 4 * pipelined.page_size;
    pipelined.copy_prefetch_window_pages = 8;
    const auto [pipelined_time, pipelined_high_watermark] =
        run_full_stripe_gc(pipelined);
    CHECK(pipelined_high_watermark > pipelined.page_size);
    CHECK(pipelined_high_watermark <= pipelined.copy_buffer_size);
    CHECK(pipelined_time < serial_time);
  }
}
