#include "hbfsim/core.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace hbfsim {
namespace {

const char* boolean(bool value) { return value ? "true" : "false"; }

const char* mapping_name(MappingPolicy value) {
  switch (value) {
    case MappingPolicy::Linear: return "linear";
    case MappingPolicy::FineStripe: return "fine_stripe";
    case MappingPolicy::BurstStripe: return "burst_stripe";
    case MappingPolicy::HostManaged: return "host_managed";
  }
  return "unknown";
}

const char* initialization_name(InitializationMode value) {
  switch (value) {
    case InitializationMode::Empty: return "empty";
    case InitializationMode::ImageLoaded: return "image_loaded";
    case InitializationMode::Preconditioned: return "preconditioned";
  }
  return "unknown";
}

const char* gc_policy_name(HostGcVictimPolicy value) {
  switch (value) {
    case HostGcVictimPolicy::InvalidRatio: return "invalid_ratio";
    case HostGcVictimPolicy::Greedy: return "greedy";
  }
  return "unknown";
}

}  // namespace

void Config::write_resolved_yaml(const std::string& path) const {
  const auto target = std::filesystem::path(path);
  if (target.has_parent_path())
    std::filesystem::create_directories(target.parent_path());
  std::ofstream out(target);
  if (!out) throw std::runtime_error("cannot write resolved config");
  out << "device:\n  stacks: " << stacks
      << "\nhost_interface:\n  channels_per_stack: "
      << host_channels_per_stack
      << "\n  bandwidth_per_channel: " << host_bw_bytes_per_ns
      << "GBps\n  fixed_latency_ns: " << host_fixed_latency_ns
      << "\n  full_duplex: " << boolean(host_full_duplex)
      << "\nnand:\n  dies_per_stack: " << dies_per_stack
      << "\n  planes_per_die: " << planes_per_die
      << "\n  blocks_per_plane: " << blocks_per_plane
      << "\n  pages_per_block: " << pages_per_block
      << "\n  page_size: " << page_size
      << "\n  strict_media_validation: " << boolean(strict_media_validation)
      << "\n  timing:\n    read_ns: " << read_ns
      << "\n    program_ns: " << program_ns
      << "\n    erase_ns: " << erase_ns
      << "\n    t_ccs_ns: " << t_ccs_ns
      << "\n    t_adl_ns: " << t_adl_ns
      << "\n    t_whr_ns: " << t_whr_ns
      << "\n    suspend_ns: " << suspend_ns
      << "\n    resume_ns: " << resume_ns
      << "\n    multi_plane_setup_ns: " << multi_plane_setup_ns
      << "\n    cache_program_setup_ns: " << cache_program_setup_ns
      << "\n    read_retry_ns: " << read_retry_ns
      << "\n  parallelism:\n    max_active_planes_per_die: "
      << max_active_planes_per_die
      << "\n    max_active_planes_per_stack: "
      << max_active_planes_per_stack
      << "\n  features:\n    suspend_resume: "
      << boolean(suspend_resume_enabled)
      << "\n    multi_plane: " << boolean(multi_plane_enabled)
      << "\n    max_multi_plane_width: " << max_multi_plane_width
      << "\n    cache_program: " << boolean(cache_program_enabled)
      << "\n  reliability:\n    program_failure_rate: "
      << program_failure_rate
      << "\n    program_failure_rate_per_erase: "
      << program_failure_rate_per_erase
      << "\n    program_failure_budget: " << program_failure_budget
      << "\n    erase_failure_rate: " << erase_failure_rate
      << "\n    erase_failure_rate_per_erase: "
      << erase_failure_rate_per_erase
      << "\n    raw_bit_error_rate: " << raw_bit_error_rate
      << "\n    raw_bit_error_rate_per_erase: "
      << raw_bit_error_rate_per_erase
      << "\n    retry_ber_multiplier: " << retry_ber_multiplier
      << "\n    ecc_correctable_bits: " << ecc_correctable_bits
      << "\n    max_read_retries: " << max_read_retries
      << "\n    max_erase_cycles: " << max_erase_cycles
      << "\n    random_seed: " << random_seed
      << "\ninternal_fabric:\n  ports_per_stack: " << ports_per_stack
      << "\n  aggregate_bandwidth: " << internal_bw_bytes_per_ns
      << "GBps\n  port_bandwidth: " << internal_port_bw_bytes_per_ns
      << "GBps\n  fixed_latency_ns: " << internal_fixed_latency_ns
      << "\ninitialization:\n  mode: "
      << initialization_name(initialization_mode)
      << "\nmapping:\n  policy: " << mapping_name(mapping_policy)
      << "\n  burst_size: " << burst_size
      << "\nscheduler:\n  write_starvation_ns: "
      << write_starvation_ns
      << "\n  source_aging_ns: " << source_aging_ns
      << "\n  max_consecutive_reads: " << max_consecutive_reads
      << "\nhost_management:\n  auto_recovery: "
      << boolean(auto_recovery_enabled)
      << "\n  max_recovery_attempts: " << max_recovery_attempts
      << "\nhost_gc:\n  enabled: " << boolean(host_gc_enabled)
      << "\n  low_watermark: " << host_gc_low_watermark
      << "\n  high_watermark: " << host_gc_high_watermark
      << "\n  overprovisioning_ratio: "
      << host_gc_overprovisioning_ratio
      << "\n  victim_policy: " << gc_policy_name(host_gc_victim_policy)
      << "\nrefresh:\n  enabled: " << boolean(automatic_refresh_enabled)
      << "\n  retention_time_ns: " << retention_time_ns
      << "\n  guard_time_ns: " << refresh_guard_time_ns
      << "\n  max_concurrent_jobs: " << max_concurrent_refresh_jobs
      << "\ncopy_engine:\n  max_inflight_reads: "
      << copy_max_inflight_reads
      << "\n  max_inflight_programs: " << copy_max_inflight_programs
      << "\n  copy_buffer_size: " << copy_buffer_size
      << "\n  prefetch_window_pages: " << copy_prefetch_window_pages
      << "\nsimulation:\n  max_requests: " << max_requests
      << "\n  warmup_requests: " << warmup_requests
      << "\nstatistics:\n  queue_depth_sample_interval_ns: "
      << queue_depth_sample_interval_ns
      << "\n  output_dir: " << output_dir << '\n';
}

}  // namespace hbfsim
