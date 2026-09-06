#include "hbfsim/config/config.h"

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

const char* channel_mapping_name(ChannelMediaPolicy value) {
  switch (value) {
    case ChannelMediaPolicy::Linear: return "linear";
    case ChannelMediaPolicy::FineStripe: return "fine_stripe";
  }
  return "unknown";
}

const char* stripe_scope_name(StripeScope value) {
  switch (value) {
    case StripeScope::Device: return "device";
    case StripeScope::Stack: return "stack";
    case StripeScope::Custom: return "custom";
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
  const auto planes_per_stack =
      static_cast<std::uint64_t>(dies_per_stack) * planes_per_die;
  const auto total_planes = static_cast<std::uint64_t>(stacks) *
                            planes_per_stack;
  const auto effective_lanes =
      stripe_scope == StripeScope::Stack
          ? planes_per_stack
          : stripe_scope == StripeScope::Custom ? stripe_lanes
                                                 : total_planes;
  const auto effective_hbf_channels =
      hbf_channel_count == 0
          ? static_cast<std::uint64_t>(stacks) * host_channels_per_stack
          : hbf_channel_count;
  out << "simulation:\n  profile: " << to_string(simulation_profile)
      << "\n  max_requests: " << max_requests
      << "\n  warmup_requests: " << warmup_requests
      << "\nprotocol:\n  abstraction: "
      << to_string(protocol_abstraction)
      << "\nhbf:\n  channel_count: " << hbf_channel_count
      << "\n  effective_channel_count: " << effective_hbf_channels
      << "\n  channel_interleave: " << hbf_channel_interleave
      << "\n  page0_auto_erase: " << boolean(page0_auto_erase)
      << "\n  media_mapping:\n    policy: "
      << channel_mapping_name(channel_media_policy)
      << "\n  read_cache:\n    enabled: " << boolean(read_cache_enabled)
      << "\n    entries_per_bank: " << read_cache_entries_per_bank
      << "\n  batch_read:\n    enabled: " << boolean(batch_read_enabled)
      << "\n    aggregation_window_ns: " << batch_read_aggregation_window_ns
      << "\n    max_pages: " << batch_read_max_pages
      << "\n  host_driven_read_retry: "
      << boolean(host_driven_read_retry)
      << "\n  dlu:\n    size: " << dlu_size
      << "\n    max_pending: " << max_pending_dlus
      << "\n    accumulation_timeout_ns: "
      << dlu_accumulation_timeout_ns
      << "\naxi:\n  ports_per_channel: " << axi_ports_per_channel
      << "\n  port_interleave: " << axi_port_interleave
      << "\n  id_count: " << axi_id_count
      << "\n  max_outstanding_per_id: "
      << axi_max_outstanding_per_id
      << "\nresearch_extensions:\n  stripe_mapping: "
      << boolean(research_stripe_mapping_enabled)
      << "\n  copy_gc:\n    enabled: "
      << boolean(research_copy_gc_enabled)
      << "\n  migration_recovery:\n    enabled: "
      << boolean(research_migration_recovery_enabled)
      << "\ndevice:\n  stacks: " << stacks
      << "\nhost_interface:\n  channels_per_stack: "
      << host_channels_per_stack
      << "\n  bandwidth_per_channel: " << host_bw_bytes_per_ns
      << "GBps\n  fixed_latency_ns: " << host_fixed_latency_ns
      << "\n  full_duplex: " << boolean(host_full_duplex)
      << "\nnand:\n  dies_per_stack: " << dies_per_stack
      << "\n  banks_per_die: " << banks_per_die
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
      << "\n    raw_bit_error_rate_per_read: " << raw_bit_error_rate_per_read
      << "\n    raw_bit_error_rate_per_retention_ns: "
      << raw_bit_error_rate_per_retention_ns
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
      << "\nstripe:\n  scope: " << stripe_scope_name(stripe_scope)
      << "\n  lanes: " << stripe_lanes
      << "\n  effective_lanes: " << effective_lanes
      << "\n  parallelism_groups: " << total_planes / effective_lanes
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
      << "\n  read_count_threshold: " << refresh_read_count_threshold
      << "\nzones:\n  count: " << zone_count
      << "\n  size_pages: " << zone_size_pages
      << "\nwear_leveling:\n  min_user_writes: "
      << wear_leveling_min_user_writes
      << "\n  min_pec_delta: " << wear_leveling_min_pec_delta
      << "\ncopy_engine:\n  max_inflight_reads: "
      << copy_max_inflight_reads
      << "\n  max_inflight_programs: " << copy_max_inflight_programs
      << "\n  copy_buffer_size: " << copy_buffer_size
      << "\n  prefetch_window_pages: " << copy_prefetch_window_pages
      << "\nstatistics:\n  queue_depth_sample_interval_ns: "
      << queue_depth_sample_interval_ns
      << "\n  output_dir: " << output_dir << '\n';
}

}  // namespace hbfsim
