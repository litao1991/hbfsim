#include "hbfsim/core.h"

#include "test_support.h"

#include <stdexcept>

namespace {

hbfsim::Config stripe_config() {
  hbfsim::Config config;
  config.stacks = 2;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.blocks_per_plane = 3;
  config.pages_per_block = 3;
  config.page_size = 4096;
  config.host_channels_per_stack = 1;
  config.ports_per_stack = 2;
  config.max_active_planes_per_die = 2;
  config.max_active_planes_per_stack = 2;
  config.mapping_policy = hbfsim::MappingPolicy::HostManaged;
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

}  // namespace

int main() {
  const auto config = stripe_config();

  {
    hbfsim::StripeMappingTable mapping(config);
    CHECK(mapping.stripe_width() == 4);
    CHECK(mapping.stripe_capacity() == 12);
    const auto stripe = mapping.allocate(0);
    CHECK(stripe.physical_id == 0);
    CHECK(stripe.generation == 1);

    for (std::uint32_t slot = 0; slot < mapping.stripe_capacity(); ++slot) {
      const auto paddr = mapping.reserve_program(slot);
      CHECK(paddr.page == slot / mapping.stripe_width());
      const auto lane = slot % mapping.stripe_width();
      CHECK((paddr.stack * config.planes_per_die + paddr.plane) == lane);
      CHECK(mapping.slot_of(paddr) == slot);
      mapping.commit_program(slot, paddr);
      const auto forward = mapping.lookup(slot);
      CHECK(forward.has_value());
      const auto reverse = mapping.reverse_lookup(
          *forward, forward->generation);
      CHECK(reverse.has_value() && *reverse == slot);
    }
    CHECK(mapping.descriptor(stripe).state == hbfsim::StripeState::Sealed);
    CHECK(mapping.active_mapping_count() == 1);
    CHECK(rejected([&] { mapping.reserve_program(0); }));
    mapping.invalidate(3);
    CHECK(!mapping.lookup(3).has_value());
    CHECK(mapping.descriptor(stripe).invalid_bitmap.test(3));
  }

  {
    hbfsim::StripeMappingTable mapping(config);
    const auto source = mapping.allocate(0);
    const auto source_page = mapping.reserve_program(0);
    mapping.commit_program(0, source_page);
    mapping.seal(source);
    mapping.begin_migration(source);

    const auto destination = mapping.allocate_replacement(0);
    const auto destination_page = mapping.reserve_program(destination, 0);
    mapping.commit_program(0, destination_page);
    CHECK(rejected([&] { mapping.remap_commit(source, destination); }));
    mapping.seal(destination);

    const auto before = mapping.lookup(0);
    CHECK(before.has_value() && before->physical_stripe == source.physical_id);
    mapping.remap_commit(source, destination);
    const auto after = mapping.lookup(0);
    CHECK(after.has_value() &&
          after->physical_stripe == destination.physical_id);
    CHECK(mapping.descriptor(source).state == hbfsim::StripeState::Stale);

    for (std::uint32_t lane = 0; lane < mapping.stripe_width(); ++lane)
      mapping.on_erase(mapping.address_for(source, lane));
    CHECK(!mapping.validate_generation(source_page));

    mapping.allocate(mapping.stripe_capacity());
    const auto reused = mapping.allocate(2 * mapping.stripe_capacity());
    CHECK(reused.physical_id == source.physical_id);
    CHECK(reused.generation == source.generation + 1);
    CHECK(rejected([&] { mapping.descriptor(source); }));
  }

  {
    hbfsim::StripeMappingTable mapping(config);
    CHECK(rejected([&] { mapping.allocate(1); }));
    const auto stripe = mapping.allocate(0);
    CHECK(rejected([&] { mapping.reserve_program(1); }));
    const auto failed_page = mapping.reserve_program(0);
    const auto notice = mapping.fail_program(0, failed_page);
    CHECK(notice.stripe == stripe);
    CHECK(notice.failed_slot == 0);
    CHECK(mapping.descriptor(stripe).next_program_slot == 1);
    CHECK(mapping.descriptor(stripe).failed_bitmap.test(0));
    CHECK(mapping.descriptor(stripe).state == hbfsim::StripeState::Degraded);
    CHECK(rejected([&] { mapping.reserve_program(1); }));

    mapping.begin_migration(stripe);
    const auto replacement = mapping.allocate_replacement(0);
    const auto recovered_page = mapping.reserve_program(replacement, 0);
    mapping.commit_program(0, recovered_page);
    mapping.seal(replacement);
    mapping.remap_commit(stripe, replacement);
    CHECK(mapping.lookup(0)->physical_stripe == replacement.physical_id);
  }

  {
    hbfsim::StripeMappingTable mapping(config);
    const auto source = mapping.allocate(0);
    const auto source_page = mapping.reserve_program(0);
    mapping.commit_program(0, source_page);
    mapping.seal(source);
    mapping.begin_migration(source);
    const auto destination = mapping.allocate_replacement(0);
    const auto destination_page = mapping.reserve_program(destination, 0);
    mapping.fail_program(0, destination_page);
    mapping.abort_migration(destination);
    CHECK(mapping.descriptor(source).state == hbfsim::StripeState::Sealed);
    CHECK(mapping.descriptor(destination).state == hbfsim::StripeState::Stale);
    CHECK(mapping.lookup(0)->physical_stripe == source.physical_id);
  }

  {
    auto simulator_config = config;
    simulator_config.program_failure_rate = 1.0;
    simulator_config.host_bw_bytes_per_ns = 4096;
    simulator_config.internal_bw_bytes_per_ns = 4096;
    simulator_config.internal_port_bw_bytes_per_ns = 4096;
    simulator_config.host_fixed_latency_ns = 0;
    simulator_config.internal_fixed_latency_ns = 0;
    simulator_config.program_ns = 10;
    hbfsim::Simulator simulator(simulator_config);
    simulator.submit({0, hbfsim::OpType::Write, 0, 4096, 0});
    simulator.run();
    CHECK(simulator.program_failure_notices().size() == 1);
    const auto& notice = simulator.program_failure_notices().front();
    CHECK(notice.failed_slot == 0);
    CHECK(notice.stripe.generation == 1);
    CHECK(simulator.stats().program_failure_notices() == 1);
  }
}
