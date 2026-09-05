#include "hbfsim/core.h"

#include "test_support.h"

#include <stdexcept>

namespace {

hbfsim::Config group_config() {
  hbfsim::Config config;
  config.stacks = 2;
  config.dies_per_stack = 1;
  config.planes_per_die = 4;
  config.blocks_per_plane = 3;
  config.pages_per_block = 2;
  config.page_size = 4096;
  config.host_channels_per_stack = 1;
  config.ports_per_stack = 4;
  config.max_active_planes_per_die = 4;
  config.max_active_planes_per_stack = 4;
  config.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  config.stripe_scope = hbfsim::StripeScope::Custom;
  config.stripe_lanes = 2;
  config.strict_media_validation = true;
  config.host_bw_bytes_per_ns = 4096;
  config.internal_bw_bytes_per_ns = 4096;
  config.internal_port_bw_bytes_per_ns = 4096;
  config.host_fixed_latency_ns = 0;
  config.internal_fixed_latency_ns = 0;
  config.read_ns = 10;
  config.program_ns = 20;
  config.erase_ns = 30;
  return config;
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

void fill(hbfsim::StripeMappingTable& mapping, std::uint64_t base) {
  for (std::uint32_t slot = 0; slot < mapping.stripe_capacity(); ++slot) {
    const auto address = mapping.reserve_program(base + slot);
    mapping.commit_program(base + slot, address);
  }
}

}  // namespace

int main() {
  {
    auto config = group_config();
    hbfsim::StripeMappingTable mapping(config);
    CHECK(mapping.stripe_width() == 2);
    CHECK(mapping.stripe_capacity() == 4);
    CHECK(mapping.parallelism_group_count() == 4);
    CHECK(mapping.total_stripe_count() == 12);

    const auto group0 = mapping.allocate(0);
    const auto group1 = mapping.allocate(mapping.stripe_capacity());
    const auto group2 = mapping.allocate(2 * mapping.stripe_capacity());
    const auto group3 = mapping.allocate(3 * mapping.stripe_capacity());
    CHECK(group0.physical_id == 0);
    CHECK(group1.physical_id == 3);
    CHECK(group2.physical_id == 6);
    CHECK(group3.physical_id == 9);
    CHECK(mapping.parallelism_group(group3) == 3);

    const auto g0_lane0 = mapping.address_for(group0, 0);
    const auto g1_lane0 = mapping.address_for(group1, 0);
    const auto g2_lane0 = mapping.address_for(group2, 0);
    const auto g3_lane1 = mapping.address_for(group3, 1);
    CHECK(g0_lane0.stack == 0 && g0_lane0.plane == 0);
    CHECK(g1_lane0.stack == 0 && g1_lane0.plane == 2);
    CHECK(g2_lane0.stack == 1 && g2_lane0.plane == 0);
    CHECK(g3_lane1.stack == 1 && g3_lane1.plane == 3);
    CHECK(g3_lane1.block == 0);
    CHECK(mapping.slot_of(g3_lane1) == 1);
  }

  {
    auto config = group_config();
    hbfsim::StripeMappingTable mapping(config);
    const auto source = mapping.allocate(0);
    fill(mapping, 0);
    const auto last = mapping.lookup(3);
    CHECK(last.has_value());
    CHECK(mapping.reverse_lookup(*last, source.generation) == 3);
    CHECK(mapping.free_stripe_count() == 11);
    mapping.begin_migration(source);
    for (std::uint32_t lane = 0; lane < mapping.stripe_width(); ++lane)
      mapping.on_erase(mapping.address_for(source, lane));
    CHECK(mapping.free_stripe_count() == 12);
    CHECK(!mapping.validate_generation(*last));
  }

  {
    auto config = group_config();
    hbfsim::StripeMappingTable mapping(config);
    const auto stripe = mapping.allocate(0);
    const auto retired = mapping.address_for(stripe, 0);
    CHECK(mapping.retire_stripe(retired));
    CHECK(mapping.usable_stripe_count() == 11);
    CHECK(mapping.descriptor(stripe).state == hbfsim::StripeState::Bad);
  }

  {
    auto config = group_config();
    config.stripe_scope = hbfsim::StripeScope::Stack;
    config.stripe_lanes = 0;
    hbfsim::StripeMappingTable mapping(config);
    CHECK(mapping.stripe_width() == 4);
    CHECK(mapping.parallelism_group_count() == 2);
    CHECK(mapping.total_stripe_count() == 6);
  }

  {
    auto config = group_config();
    config.stripe_scope = hbfsim::StripeScope::Device;
    config.stripe_lanes = 0;
    hbfsim::StripeMappingTable mapping(config);
    CHECK(mapping.stripe_width() == 8);
    CHECK(mapping.parallelism_group_count() == 1);
    CHECK(mapping.total_stripe_count() == config.blocks_per_plane);
  }

  {
    auto config = group_config();
    config.stripe_lanes = 3;
    CHECK(rejected([&] { config.validate(); }));
    config.stripe_scope = hbfsim::StripeScope::Device;
    config.stripe_lanes = 2;
    CHECK(rejected([&] { config.validate(); }));
    config = group_config();
    config.stacks = 2;
    config.planes_per_die = 6;
    config.max_active_planes_per_die = 6;
    config.max_active_planes_per_stack = 6;
    config.stripe_lanes = 4;
    CHECK(rejected([&] { config.validate(); }));
  }

  {
    const auto config = group_config();
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Write, 0,
                      2 * config.page_size, 0});
    simulator.submit({100, hbfsim::OpType::Write,
                      4 * config.page_size, 2 * config.page_size, 0});
    simulator.run();
    CHECK(simulator.stats().failed_requests() == 0);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) ==
          hbfsim::PageState::Valid);
    CHECK(simulator.page_state({0, 0, 1, 0, 0}) ==
          hbfsim::PageState::Valid);
    CHECK(simulator.page_state({0, 0, 2, 0, 0}) ==
          hbfsim::PageState::Valid);
    CHECK(simulator.page_state({0, 0, 3, 0, 0}) ==
          hbfsim::PageState::Valid);
  }

  {
    auto config = group_config();
    config.stacks = 1;
    config.blocks_per_plane = 4;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Write, 0,
                      4 * config.page_size, 0});
    simulator.run();
    simulator.start_host_gc(0);
    simulator.run();
    CHECK(simulator.stats().completed_gc_jobs() == 1);
    CHECK(simulator.stats().remap_commits() == 1);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) ==
          hbfsim::PageState::Erased);
    CHECK(simulator.page_state({0, 0, 1, 0, 0}) ==
          hbfsim::PageState::Erased);
    CHECK(simulator.page_state({0, 0, 2, 0, 0}) ==
          hbfsim::PageState::Valid);
    CHECK(simulator.page_state({0, 0, 3, 0, 0}) ==
          hbfsim::PageState::Valid);
  }

  {
    auto config = group_config();
    config.stacks = 1;
    config.blocks_per_plane = 4;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Write, 0,
                      4 * config.page_size, 0});
    simulator.run();
    simulator.start_refresh(0);
    simulator.run();
    CHECK(simulator.stats().completed_refresh_jobs() == 1);
    CHECK(simulator.stats().remap_commits() == 1);
    CHECK(simulator.stats().source_bytes(
              hbfsim::TransactionSource::Refresh,
              hbfsim::OpType::Read) == 4 * config.page_size);
  }

  {
    auto config = group_config();
    config.stacks = 1;
    config.blocks_per_plane = 4;
    config.max_erase_cycles = 1;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Write, 0,
                      2 * config.page_size, 0});
    simulator.submit({100, hbfsim::OpType::Erase, 0, 0, 0});
    simulator.run();
    CHECK(simulator.stats().retired_blocks() == 2);
    CHECK(simulator.stats().retired_stripes() == 1);
    CHECK(simulator.block_state({0, 0, 0, 0, 0}) ==
          hbfsim::BlockState::Bad);
    CHECK(simulator.block_state({0, 0, 1, 0, 0}) ==
          hbfsim::BlockState::Bad);
    CHECK(simulator.block_state({0, 0, 2, 0, 0}) ==
          hbfsim::BlockState::Free);
    const auto physical_capacity = static_cast<std::uint64_t>(
        config.planes_per_die) * config.blocks_per_plane *
        config.pages_per_block * config.page_size;
    const auto stripe_capacity = static_cast<std::uint64_t>(
        config.stripe_lanes) * config.pages_per_block * config.page_size;
    CHECK(simulator.stats().usable_physical_capacity_bytes() ==
          physical_capacity - stripe_capacity);
  }
}
