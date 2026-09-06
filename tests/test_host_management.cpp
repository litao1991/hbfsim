#include "hbfsim/core.h"

#include <cstdlib>
#include <iostream>

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed: " #condition << " at " << __FILE__ << ':' \
                << __LINE__ << '\n';                                       \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (false)

namespace {

hbfsim::Config config() {
  hbfsim::Config value;
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 1;
  value.blocks_per_plane = 6;
  value.pages_per_block = 2;
  value.page_size = 4096;
  value.host_channels_per_stack = 1;
  value.ports_per_stack = 1;
  value.max_active_planes_per_die = 1;
  value.max_active_planes_per_stack = 1;
  value.mapping_policy = hbfsim::MappingPolicy::HostManaged;
  value.zone_count = 2;
  value.zone_size_pages = 2;
  value.host_fixed_latency_ns = 0;
  value.internal_fixed_latency_ns = 0;
  value.host_bw_bytes_per_ns = 1000;
  value.internal_bw_bytes_per_ns = 1000;
  return value;
}

}  // namespace

int main() {
  {
    hbfsim::Simulator simulator(config());
    simulator.submit({0, hbfsim::OpType::Write, 0, 8192, 0});
    simulator.run();
    const auto before = simulator.system().mapper().lookup(0);
    CHECK(before.has_value());
    simulator.start_host_refresh(0);
    simulator.run();
    const auto after = simulator.system().mapper().lookup(0);
    CHECK(after.has_value());
    CHECK(after->physical_stripe != before->physical_stripe);
    CHECK(simulator.stats().source_bytes(hbfsim::TransactionSource::HostRefresh,
                                         hbfsim::OpType::Write) == 8192);
    CHECK(simulator.stats().host_rewrite_jobs(
              hbfsim::TransactionSource::HostRefresh, false) == 1);
  }
  {
    hbfsim::Simulator simulator(config());
    simulator.submit({0, hbfsim::OpType::Write, 0, 8192, 0});
    simulator.run();
    const auto before = simulator.system().mapper().lookup(0);
    CHECK(before.has_value());
    simulator.system().zones().record_physical_erase(*before, 100);
    const auto plan = simulator.start_host_wear_leveling();
    CHECK(plan.has_value());
    CHECK(plan->logical_zone == 0);
    CHECK(plan->destination_physical_zone == 1);
    simulator.run();
    const auto mapped = simulator.system().mapper().lookup(0);
    CHECK(mapped.has_value());
    CHECK(mapped->physical_stripe >= 3);
    CHECK(simulator.stats().source_bytes(
              hbfsim::TransactionSource::HostWearLevel,
              hbfsim::OpType::Write) == 8192);
    CHECK(simulator.stats().host_rewrite_jobs(
              hbfsim::TransactionSource::HostWearLevel, false) == 1);
  }
  {
    auto reduced_config = config();
    reduced_config.erase_failure_rate = 1.0;
    hbfsim::Simulator simulator(reduced_config);
    simulator.submit({0, hbfsim::OpType::Write, 0, 4096, 0});
    simulator.submit({100000, hbfsim::OpType::Erase, 0, 4096, 0});
    simulator.run();
    const auto report = simulator.reduced_capacity();
    CHECK(report.retired_blocks == 1);
    CHECK(report.retired_stripes == 1);
    CHECK(!report.retired_stripe_bitmap.empty());
    CHECK(report.retired_stripe_bitmap.at(0));
  }
  return EXIT_SUCCESS;
}
