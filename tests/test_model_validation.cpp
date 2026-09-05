#include "hbfsim/core.h"

#include "test_support.h"

#include <cstdint>
#include <vector>

namespace {

hbfsim::Config scaling_config(std::uint32_t planes) {
  hbfsim::Config config;
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = planes;
  config.blocks_per_plane = 4;
  config.pages_per_block = 64;
  config.page_size = 4096;
  config.host_channels_per_stack = planes;
  config.ports_per_stack = planes;
  config.max_active_planes_per_die = planes;
  config.max_active_planes_per_stack = planes;
  config.mapping_policy = hbfsim::MappingPolicy::FineStripe;
  config.initialization_mode = hbfsim::InitializationMode::ImageLoaded;
  config.strict_media_validation = true;
  config.host_bw_bytes_per_ns = 1'000'000;
  config.internal_bw_bytes_per_ns = 1'000'000;
  config.internal_port_bw_bytes_per_ns = 1'000'000;
  config.host_fixed_latency_ns = 0;
  config.internal_fixed_latency_ns = 0;
  config.read_ns = 1'000;
  config.program_ns = 5'000;
  config.erase_ns = 20'000;
  return config;
}

hbfsim::SimTime saturated_write_time(std::uint32_t planes) {
  auto config = scaling_config(planes);
  hbfsim::Simulator simulator(config);
  constexpr std::uint32_t pages = 64;
  for (std::uint32_t page = 0; page < pages; ++page)
    simulator.submit({0, hbfsim::OpType::Write,
                      static_cast<std::uint64_t>(page) * config.page_size,
                      config.page_size, 0});
  simulator.run();
  CHECK(simulator.stats().failed_requests() == 0);
  CHECK(simulator.stats().completed_requests() == pages);
  return simulator.now();
}

long double read_rate(std::uint32_t request_pages) {
  constexpr std::uint32_t planes = 8;
  auto config = scaling_config(planes);
  hbfsim::Simulator simulator(config);
  const auto bytes = static_cast<std::uint64_t>(request_pages) *
                     config.page_size;
  simulator.submit({0, hbfsim::OpType::Read, 0, bytes, 0});
  simulator.run();
  CHECK(simulator.stats().failed_requests() == 0);
  return static_cast<long double>(bytes) /
         static_cast<long double>(simulator.now());
}

}  // namespace

int main() {
  std::vector<hbfsim::SimTime> elapsed;
  for (const auto planes : {1U, 2U, 4U, 8U})
    elapsed.push_back(saturated_write_time(planes));
  for (std::size_t index = 1; index < elapsed.size(); ++index) {
    CHECK(elapsed[index] < elapsed[index - 1]);
    CHECK(elapsed[index] * 3 < elapsed[index - 1] * 2);
  }

  CHECK(read_rate(8) > read_rate(1) * 4.0L);
}
