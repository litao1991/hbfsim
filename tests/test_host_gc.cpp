#include "hbfsim/core.h"

#include "test_support.h"

#include <cstdint>
#include <stdexcept>

namespace {

hbfsim::Config gc_config() {
  hbfsim::Config config;
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.blocks_per_plane = 5;
  config.pages_per_block = 2;
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
  config.host_gc_enabled = true;
  config.host_gc_low_watermark = 0.20;
  config.host_gc_high_watermark = 0.40;
  config.host_gc_overprovisioning_ratio = 0.20;
  return config;
}

void submit_visible_capacity(hbfsim::Simulator& simulator,
                             const hbfsim::Config& config) {
  const std::uint64_t stripe_pages =
      static_cast<std::uint64_t>(config.stacks) *
      config.dies_per_stack * config.planes_per_die *
      config.pages_per_block;
  const std::uint64_t visible_stripes = 4;
  simulator.submit({0, hbfsim::OpType::Write, 0,
                    visible_stripes * stripe_pages * config.page_size, 0});
}

template <typename Function>
bool rejected(Function&& function) {
  try {
    function();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

void fill_mapping_stripe(hbfsim::StripeMappingTable& mapping,
                         std::uint64_t base) {
  for (std::uint32_t slot = 0; slot < mapping.stripe_capacity(); ++slot) {
    const auto paddr = mapping.reserve_program(base + slot);
    mapping.commit_program(base + slot, paddr);
  }
}

}  // namespace

int main() {
  CHECK(hbfsim::parse_op("TRIM") == hbfsim::OpType::Invalidate);

  {
    auto config = gc_config();
    config.host_gc_overprovisioning_ratio = 0.0;
    config.host_gc_low_watermark = 0.40;
    config.host_gc_high_watermark = 0.60;
    hbfsim::StripeMappingTable mapping(config);
    fill_mapping_stripe(mapping, 0);
    fill_mapping_stripe(mapping, mapping.stripe_capacity());
    fill_mapping_stripe(mapping, 2 * mapping.stripe_capacity());
    mapping.invalidate(0);
    mapping.invalidate(1);
    mapping.invalidate(2);
    mapping.invalidate(mapping.stripe_capacity());

    hbfsim::HostGcManager manager(config);
    const auto decision = manager.poll(mapping, false);
    CHECK(decision.cycle_started);
    CHECK(decision.decision.has_value());
    CHECK(decision.decision->victim.physical_id == 0);
    CHECK(!decision.decision->erase_only);
  }

  {
    auto config = gc_config();
    config.host_gc_overprovisioning_ratio = 0.0;
    config.host_gc_low_watermark = 0.60;
    config.host_gc_high_watermark = 0.80;
    config.host_gc_victim_policy = hbfsim::HostGcVictimPolicy::Greedy;
    hbfsim::StripeMappingTable mapping(config);
    fill_mapping_stripe(mapping, 0);
    mapping.invalidate(0);
    mapping.invalidate(1);
    const auto second_base = mapping.stripe_capacity();
    const auto second = mapping.allocate(second_base);
    for (std::uint32_t slot = 0; slot < 2; ++slot) {
      const auto paddr = mapping.reserve_program(second_base + slot);
      mapping.commit_program(second_base + slot, paddr);
    }
    mapping.seal(second);
    mapping.invalidate(second_base);

    hbfsim::HostGcManager manager(config);
    const auto decision = manager.poll(mapping, false);
    CHECK(decision.decision.has_value());
    CHECK(decision.decision->victim.physical_id == second.physical_id);
  }

  {
    const auto config = gc_config();
    hbfsim::StripeMappingTable mapping(config);
    CHECK(mapping.total_stripe_count() == 5);
    CHECK(mapping.host_visible_stripe_count() == 4);
    for (std::uint64_t stripe = 0; stripe < 4; ++stripe)
      mapping.allocate(stripe * mapping.stripe_capacity());
    CHECK(rejected([&] {
      mapping.allocate(4 * mapping.stripe_capacity());
    }));
  }

  {
    const auto config = gc_config();
    hbfsim::Simulator simulator(config);
    submit_visible_capacity(simulator, config);
    simulator.run();
    simulator.submit({simulator.now(), hbfsim::OpType::Invalidate, 0,
                      4 * config.page_size, 0});
    simulator.run();

    CHECK(simulator.active_copy_jobs() == 0);
    CHECK(simulator.stats().automatic_gc_jobs() == 1);
    CHECK(simulator.stats().automatic_gc_erase_only_jobs() == 1);
    CHECK(simulator.stats().completed_gc_jobs() == 1);
    CHECK(simulator.stats().host_gc_cycles_started() >= 1);
    CHECK(simulator.stats().host_gc_high_watermark_reached() == 1);
    CHECK(simulator.stats().remap_commits() == 0);
    CHECK(simulator.stats().min_free_stripes() == 1);
    CHECK(simulator.stats().source_bytes(
              hbfsim::TransactionSource::GarbageCollection,
              hbfsim::OpType::Read) == 0);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) ==
          hbfsim::PageState::Erased);
  }

  {
    const auto config = gc_config();
    hbfsim::Simulator simulator(config);
    submit_visible_capacity(simulator, config);
    simulator.run();
    simulator.submit({simulator.now(), hbfsim::OpType::Invalidate, 0,
                      2 * config.page_size, 0});
    simulator.run();

    CHECK(simulator.active_copy_jobs() == 0);
    CHECK(simulator.stats().automatic_gc_jobs() == 1);
    CHECK(simulator.stats().automatic_gc_erase_only_jobs() == 0);
    CHECK(simulator.stats().completed_gc_jobs() == 1);
    CHECK(simulator.stats().host_gc_high_watermark_reached() == 0);
    CHECK(simulator.stats().remap_commits() == 1);
    CHECK(simulator.stats().host_gc_stalls() >= 1);
    CHECK(simulator.stats().min_free_stripes() == 0);
    CHECK(simulator.stats().source_bytes(
              hbfsim::TransactionSource::GarbageCollection,
              hbfsim::OpType::Read) == 2 * config.page_size);
  }
}
