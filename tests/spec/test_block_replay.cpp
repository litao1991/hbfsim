#include "hbfsim/core.h"

#include "../test_support.h"

namespace {

hbfsim::Config config() {
  using namespace hbfsim;
  auto value = Config::for_profile(SimulationProfile::HbfV07);
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 2;
  value.banks_per_die = 1;
  value.blocks_per_plane = 2;
  value.pages_per_block = 4;
  value.host_channels_per_stack = 1;
  value.hbf_channel_count = 1;
  value.ports_per_stack = 2;
  value.max_active_planes_per_die = 2;
  value.max_active_planes_per_stack = 2;
  value.initialization_mode = InitializationMode::ImageLoaded;
  value.read_cache_enabled = false;
  value.host_fixed_latency_ns = 0;
  value.internal_fixed_latency_ns = 0;
  value.host_bw_bytes_per_ns = 1000;
  value.internal_bw_bytes_per_ns = 1000;
  value.internal_port_bw_bytes_per_ns = 1000;
  value.validate();
  return value;
}

}  // namespace

int main() {
  using namespace hbfsim;
  auto value = config();
  Simulator simulator(value);

  // local DLU 2 belongs to the same lane/block as 0. R5 is two lanes, so
  // a complete block replay emits local/global pages 0, 2, 4, and 6.
  const auto& geometry = simulator.system().spec_block_addressing().geometry();
  CHECK(geometry.r1_core_dies == 2);
  CHECK(geometry.r2_banks_per_die == 1);
  CHECK(geometry.r3_pages_per_block == 4);
  CHECK(geometry.r4_64b_units_per_page == 64);
  CHECK(geometry.r5_data_stripe_width == 2);

  simulator.start_host_refresh(2 * value.page_size);
  CHECK(simulator.spec_replay_plans().size() == 1);
  const auto& plan = simulator.spec_replay_plans().front();
  CHECK(plan.reason == HostRewriteReason::Refresh);
  CHECK(plan.page0_global_address == 0);
  CHECK(plan.failed_page == 1);
  CHECK(plan.page_global_addresses.size() == value.pages_per_block);
  CHECK(plan.page_global_addresses.at(0) == 0);
  CHECK(plan.page_global_addresses.at(1) == 2 * value.page_size);
  CHECK(plan.page_global_addresses.at(3) == 6 * value.page_size);

  simulator.run();
  CHECK(simulator.active_host_replay_jobs() == 0);
  CHECK(simulator.stats().source_bytes(TransactionSource::HostRefresh,
                                       OpType::Write) ==
        static_cast<std::uint64_t>(value.pages_per_block) * value.page_size);
  CHECK(simulator.stats().host_rewrite_jobs(TransactionSource::HostRefresh,
                                            false) == 1);

  // In the default Spec profile a visible Program Failure creates the same
  // R1-R5 Host-facing plan; it does not fall back to research Stripe replay.
  auto failing = config();
  failing.program_failure_rate = 1.0;
  Simulator failed_program(failing);
  failed_program.submit({0, OpType::Write, 0, failing.page_size, 0, 1, 0});
  failed_program.run();
  CHECK(failed_program.responses().size() == 1);
  CHECK(failed_program.responses().front().status ==
        HbfStatus::ProgramFailureReplayRequired);
  CHECK(failed_program.replay_plans().empty());
  CHECK(failed_program.spec_replay_plans().size() == 1);
  CHECK(failed_program.spec_replay_plans().front().reason ==
        HostRewriteReason::ProgramFailure);
  CHECK(failed_program.spec_replay_plans().front().page_global_addresses.at(3) ==
        6 * failing.page_size);
  return 0;
}
