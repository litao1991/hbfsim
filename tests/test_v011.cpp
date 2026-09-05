#include "hbfsim/core.h"

#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

class VectorSource final : public hbfsim::IRequestSource {
 public:
  explicit VectorSource(std::vector<hbfsim::TraceEntry> entries)
      : entries_(std::move(entries)) {}

  bool next(hbfsim::TraceEntry& entry) override {
    if (position_ == entries_.size()) return false;
    entry = entries_.at(position_++);
    return true;
  }

 private:
  std::vector<hbfsim::TraceEntry> entries_;
  std::size_t position_ = 0;
};

hbfsim::Config base_config() {
  hbfsim::Config config;
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.blocks_per_plane = 2;
  config.pages_per_block = 8;
  config.page_size = 100;
  config.host_channels_per_stack = 2;
  config.ports_per_stack = 1;
  config.max_active_planes_per_die = 2;
  config.max_active_planes_per_stack = 2;
  config.mapping_policy = hbfsim::MappingPolicy::FineStripe;
  config.host_bw_bytes_per_ns = 1000;
  config.internal_bw_bytes_per_ns = 1000;
  config.internal_port_bw_bytes_per_ns = 1000;
  config.host_fixed_latency_ns = 0;
  config.internal_fixed_latency_ns = 0;
  config.read_ns = 10;
  config.program_ns = 100;
  return config;
}

}  // namespace

int main() {
  {
    const auto path = std::filesystem::current_path() / "v011-config.yaml";
    std::ofstream config_file(path);
    config_file << "host_interface:\n"
                   "  full_duplex: false\n"
                   "internal_fabric:\n"
                   "  port_bandwidth: 64GBps\n"
                   "host_management:\n"
                   "  auto_recovery: true\n"
                   "  max_recovery_attempts: 5\n"
                   "scheduler:\n"
                   "  source_aging_ns: 1234\n"
                   "nand:\n"
                   "  reliability:\n"
                   "    program_failure_budget: 2\n"
                   "initialization:\n"
                   "  mode: preconditioned\n";
    config_file.close();
    const auto parsed = hbfsim::Config::from_yaml_file(path.string());
    CHECK(!parsed.host_full_duplex);
    CHECK(parsed.internal_port_bw_bytes_per_ns == 64.0);
    CHECK(parsed.initialization_mode ==
          hbfsim::InitializationMode::Preconditioned);
    CHECK(parsed.auto_recovery_enabled);
    CHECK(parsed.max_recovery_attempts == 5);
    CHECK(parsed.source_aging_ns == 1234);
    CHECK(parsed.program_failure_budget == 2);
  }

  {
    auto config = base_config();
    config.strict_media_validation = true;
    config.initialization_mode = hbfsim::InitializationMode::ImageLoaded;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Read, 0, 100, 0});
    simulator.run();
    CHECK(simulator.stats().failed_requests() == 0);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) ==
          hbfsim::PageState::Valid);
  }

  {
    auto config = base_config();
    config.strict_media_validation = true;
    config.initialization_mode = hbfsim::InitializationMode::Empty;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Read, 0, 100, 0});
    bool rejected = false;
    try {
      simulator.run();
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    CHECK(rejected);
  }

  {
    auto config = base_config();
    config.strict_media_validation = true;
    config.initialization_mode = hbfsim::InitializationMode::ImageLoaded;
    config.erase_ns = 20;
    hbfsim::Simulator simulator(config);
    simulator.submit({0, hbfsim::OpType::Read, 0, 100, 0});
    simulator.submit({30, hbfsim::OpType::Erase, 0, 0, 0});
    simulator.submit({100, hbfsim::OpType::Read, 0, 100, 0});
    bool rejected_after_erase = false;
    try {
      simulator.run();
    } catch (const std::runtime_error&) {
      rejected_after_erase = true;
    }
    CHECK(rejected_after_erase);
  }

  {
    auto config = base_config();
    config.warmup_requests = 1;
    config.strict_media_validation = true;
    config.initialization_mode = hbfsim::InitializationMode::ImageLoaded;
    VectorSource source({{0, hbfsim::OpType::Write, 0, 100, 0},
                         {0, hbfsim::OpType::Read, 100, 100, 0}});
    hbfsim::Simulator simulator(config);
    simulator.run(source);
    CHECK(simulator.stats().completed_requests() == 1);
    CHECK(simulator.stats().mean_latency_ns() < 20.0);
    CHECK(simulator.phase() == hbfsim::SimulationPhase::Drain);

    const auto output = std::filesystem::current_path() / "v011-results";
    simulator.stats().write(output.string(), simulator.now());
    CHECK(std::filesystem::exists(output / "latency_breakdown.csv"));
    CHECK(std::filesystem::exists(output /
                                  "source_latency_breakdown.csv"));
    CHECK(std::filesystem::exists(output / "resource_utilization.csv"));
    CHECK(std::filesystem::exists(output / "queue_depth.csv"));
    std::ifstream summary(output / "summary.csv");
    const std::string contents((std::istreambuf_iterator<char>(summary)),
                               std::istreambuf_iterator<char>());
    CHECK(contents.find("p99_9_latency_ns") != std::string::npos);
  }
}
