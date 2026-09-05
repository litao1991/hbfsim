#pragma once

#include "hbfsim/common/types.h"
#include <cstdint>
#include <string>

namespace hbfsim {

struct Config {
  SimulationProfile simulation_profile = SimulationProfile::MediaResearch;
  ProtocolAbstraction protocol_abstraction =
      ProtocolAbstraction::Transaction;
  bool research_stripe_mapping_enabled = true;
  bool research_copy_gc_enabled = true;
  bool research_migration_recovery_enabled = true;
  std::uint32_t hbf_channel_count = 0;
  std::uint64_t hbf_channel_interleave = 4 * 1024;
  std::uint32_t axi_ports_per_channel = 1;
  std::uint64_t axi_port_interleave = 64;
  std::uint32_t axi_id_count = 256;
  std::uint32_t axi_max_outstanding_per_id = 16;
  std::uint64_t dlu_size = 4 * 1024;
  std::uint32_t max_pending_dlus = 64;
  SimTime dlu_accumulation_timeout_ns = 1'000'000;
  bool page0_auto_erase = true;
  ChannelMediaPolicy channel_media_policy = ChannelMediaPolicy::Linear;
  std::uint32_t banks_per_die = 1;
  bool read_cache_enabled = false;
  std::uint32_t read_cache_entries_per_bank = 2;
  bool batch_read_enabled = false;
  SimTime batch_read_aggregation_window_ns = 0;
  std::uint32_t batch_read_max_pages = 1;
  bool host_driven_read_retry = false;
  std::uint32_t stacks = 1;
  std::uint32_t dies_per_stack = 16;
  std::uint32_t planes_per_die = 32;
  std::uint32_t blocks_per_plane = 256;
  std::uint32_t pages_per_block = 1024;
  std::uint64_t page_size = 4 * 1024;
  std::uint32_t host_channels_per_stack = 4;
  std::uint32_t ports_per_stack = 32;
  std::uint32_t max_active_planes_per_die = 32;
  std::uint32_t max_active_planes_per_stack = 512;
  SimTime read_ns = 2'000;
  SimTime program_ns = 50'000;
  SimTime erase_ns = 3'000'000;
  SimTime t_ccs_ns = 0;
  SimTime t_adl_ns = 0;
  SimTime t_whr_ns = 0;
  SimTime suspend_ns = 0;
  SimTime resume_ns = 0;
  SimTime multi_plane_setup_ns = 0;
  SimTime cache_program_setup_ns = 0;
  SimTime read_retry_ns = 0;
  SimTime host_fixed_latency_ns = 50;
  SimTime internal_fixed_latency_ns = 20;
  double host_bw_bytes_per_ns = 256.0;
  double internal_bw_bytes_per_ns = 1000.0;
  double internal_port_bw_bytes_per_ns = 1000.0;
  bool host_full_duplex = true;
  InitializationMode initialization_mode = InitializationMode::Empty;
  MappingPolicy mapping_policy = MappingPolicy::BurstStripe;
  std::uint64_t burst_size = 2 * 1024 * 1024;
  StripeScope stripe_scope = StripeScope::Device;
  std::uint32_t stripe_lanes = 0;
  SimTime write_starvation_ns = 100'000;
  SimTime source_aging_ns = 1'000'000;
  std::uint32_t max_consecutive_reads = 64;
  bool auto_recovery_enabled = false;
  std::uint32_t max_recovery_attempts = 3;
  bool host_gc_enabled = false;
  double host_gc_low_watermark = 0.10;
  double host_gc_high_watermark = 0.20;
  double host_gc_overprovisioning_ratio = 0.0;
  HostGcVictimPolicy host_gc_victim_policy =
      HostGcVictimPolicy::InvalidRatio;
  bool automatic_refresh_enabled = false;
  SimTime retention_time_ns = 0;
  SimTime refresh_guard_time_ns = 0;
  std::uint32_t max_concurrent_refresh_jobs = 1;
  std::uint32_t copy_max_inflight_reads = 32;
  std::uint32_t copy_max_inflight_programs = 8;
  std::uint64_t copy_buffer_size = 2 * 1024 * 1024;
  std::uint32_t copy_prefetch_window_pages = 64;
  bool strict_media_validation = false;
  bool suspend_resume_enabled = false;
  bool multi_plane_enabled = false;
  std::uint32_t max_multi_plane_width = 2;
  bool cache_program_enabled = false;
  double program_failure_rate = 0.0;
  double program_failure_rate_per_erase = 0.0;
  std::uint64_t program_failure_budget = 0;
  double erase_failure_rate = 0.0;
  double erase_failure_rate_per_erase = 0.0;
  double raw_bit_error_rate = 0.0;
  double raw_bit_error_rate_per_erase = 0.0;
  double raw_bit_error_rate_per_read = 0.0;
  double raw_bit_error_rate_per_retention_ns = 0.0;
  std::uint64_t refresh_read_count_threshold = 0;
  double retry_ber_multiplier = 0.25;
  std::uint32_t ecc_correctable_bits = 0;
  std::uint32_t max_read_retries = 0;
  std::uint32_t max_erase_cycles = 0;
  std::uint64_t random_seed = 1;
  std::uint64_t max_requests = 0;
  std::uint64_t warmup_requests = 0;
  SimTime queue_depth_sample_interval_ns = 1'000;
  std::string output_dir = "results";

  static Config for_profile(SimulationProfile profile);
  void apply_profile_defaults(SimulationProfile profile);
  static Config from_yaml_file(const std::string& path);
  void validate() const;
  void write_resolved_yaml(const std::string& path) const;
};

}  // namespace hbfsim
