#include "hbfsim/core.h"

#include "../test_support.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path write_config(const std::string& name,
                                   const std::string& contents) {
  const auto path = std::filesystem::temp_directory_path() /
                    ("hbfsim_spec_" + name + ".yaml");
  std::ofstream out(path);
  out << contents;
  return path;
}

bool rejected(const std::filesystem::path& path) {
  try {
    static_cast<void>(hbfsim::Config::from_yaml_file(path.string()));
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

int main() {
  using namespace hbfsim;

  Config legacy;
  CHECK(legacy.simulation_profile == SimulationProfile::MediaResearch);
  CHECK(legacy.protocol_abstraction == ProtocolAbstraction::Transaction);
  CHECK(legacy.research_stripe_mapping_enabled);
  CHECK(legacy.research_copy_gc_enabled);
  CHECK(legacy.research_migration_recovery_enabled);

  const auto spec_path = write_config(
      "profile",
      "simulation:\n  profile: hbf_v0_7\n"
      "protocol:\n  abstraction: transaction\n"
      "mapping:\n  policy: linear\n");
  const auto spec = Config::from_yaml_file(spec_path.string());
  CHECK(spec.simulation_profile == SimulationProfile::HbfV07);
  CHECK(!spec.research_stripe_mapping_enabled);
  CHECK(!spec.research_copy_gc_enabled);
  CHECK(!spec.research_migration_recovery_enabled);
  CHECK(spec.channel_media_policy == ChannelMediaPolicy::FineStripe);
  CHECK(spec.read_cache_enabled);
  CHECK(spec.mapping_policy == MappingPolicy::Linear);

  const auto enabled_path = write_config(
      "extension",
      "simulation:\n  profile: hbf_v0_7\n"
      "protocol:\n  abstraction: transaction\n"
      "research_extensions:\n  stripe_mapping: true\n"
      "mapping:\n  policy: host_managed\n");
  const auto enabled = Config::from_yaml_file(enabled_path.string());
  CHECK(enabled.research_stripe_mapping_enabled);

  const auto disabled_path = write_config(
      "disabled_extension",
      "simulation:\n  profile: hbf_v0_7\n"
      "mapping:\n  policy: host_managed\n");
  CHECK(rejected(disabled_path));

  const auto flit_path = write_config(
      "flit",
      "simulation:\n  profile: hbf_v0_7\n"
      "protocol:\n  abstraction: flit\n"
      "mapping:\n  policy: linear\n");
  CHECK(rejected(flit_path));

  const auto resolved_path = std::filesystem::temp_directory_path() /
                             "hbfsim_spec_resolved.yaml";
  spec.write_resolved_yaml(resolved_path.string());
  std::ifstream resolved_input(resolved_path);
  const std::string resolved((std::istreambuf_iterator<char>(resolved_input)),
                             std::istreambuf_iterator<char>());
  CHECK(resolved.find("profile: hbf_v0_7") != std::string::npos);
  CHECK(resolved.find("abstraction: transaction") != std::string::npos);
  CHECK(resolved.find("stripe_mapping: false") != std::string::npos);
  CHECK(resolved.find("policy: fine_stripe") != std::string::npos);
  CHECK(resolved.find("entries_per_bank: 2") != std::string::npos);

  std::filesystem::remove(spec_path);
  std::filesystem::remove(enabled_path);
  std::filesystem::remove(disabled_path);
  std::filesystem::remove(flit_path);
  std::filesystem::remove(resolved_path);
  return 0;
}
