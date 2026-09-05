#include "hbfsim/core.h"

#include <cstdlib>
#include <iostream>

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':' \
                << __LINE__ << '\n';                                         \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (false)

namespace {

hbfsim::Config config() {
  hbfsim::Config value;
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 1;
  value.blocks_per_plane = 4;
  value.pages_per_block = 2;
  value.max_active_planes_per_die = 1;
  value.max_active_planes_per_stack = 1;
  value.page_size = 4096;
  value.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  value.host_gc_overprovisioning_ratio = 0.25;
  value.read_ns = 5;
  value.program_ns = 10;
  value.erase_ns = 20;
  value.host_fixed_latency_ns = 0;
  value.internal_fixed_latency_ns = 0;
  value.automatic_refresh_enabled = true;
  value.retention_time_ns = 100;
  value.refresh_guard_time_ns = 20;
  value.max_concurrent_refresh_jobs = 1;
  return value;
}

}  // namespace

int main() {
  {
    auto value = config();
    hbfsim::StripeMappingTable mapping(value);
    const auto stripe = mapping.allocate(0);
    const auto first = mapping.reserve_program(stripe, 0);
    mapping.commit_program(0, first, 10);
    const auto second = mapping.reserve_program(stripe, 1);
    mapping.commit_program(1, second, 15);
    if (mapping.descriptor(stripe).state == hbfsim::StripeState::Open)
      mapping.seal(stripe);
    hbfsim::RefreshManager manager(value);
    const auto early = manager.poll(mapping, 89, 0);
    CHECK(!early.decision);
    CHECK(early.next_check_at && *early.next_check_at == 90);
    const auto due = manager.poll(mapping, 90, 0);
    CHECK(due.decision && due.decision->source == stripe);
    CHECK(due.decision->deadline == 110);
    CHECK(!due.deadline_missed);
  }

  {
    auto value = config();
    value.refresh_guard_time_ns = 0;
    hbfsim::Simulator simulator(value);
    simulator.submit({0, hbfsim::OpType::Write, 0, 8192, 0});
    simulator.submit({150, hbfsim::OpType::Read, 0, 4096, 0});
    simulator.run();
    CHECK(simulator.stats().automatic_refresh_jobs() >= 1);
    CHECK(simulator.stats().completed_refresh_jobs() >= 1);
    CHECK(simulator.stats().source_bytes(hbfsim::TransactionSource::Refresh,
                                         hbfsim::OpType::Read) >= 8192);
    CHECK(simulator.stats().source_bytes(hbfsim::TransactionSource::Refresh,
                                         hbfsim::OpType::Write) >= 8192);
    CHECK(simulator.stats().failed_requests() == 0);
  }

  return EXIT_SUCCESS;
}
