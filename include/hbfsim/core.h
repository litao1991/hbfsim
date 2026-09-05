#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hbfsim {

using SimTime = std::uint64_t;

enum class OpType { Read, Write, Erase, Refresh, Invalidate };
enum class MappingPolicy { Linear, FineStripe, BurstStripe, HostManaged };
enum class StripeScope { Device, Stack, Custom };
enum class TransactionSource {
  User,
  Mapping,
  Maintenance,
  Refresh,
  GarbageCollection,
  Recovery,
};
enum class HostGcVictimPolicy { InvalidRatio, Greedy };
enum class BlockState { Free, Open, Closed, Erasing, Bad };
enum class PageState { Erased, Reading, Programming, Valid, Invalid, Failed };
enum class StripeState {
  Free,
  Open,
  Sealed,
  Degraded,
  Migrating,
  Stale,
  Erasing,
  Bad,
};
enum class ReadErrorStatus { Clean, Corrected, Uncorrectable };
enum class InitializationMode { Empty, ImageLoaded, Preconditioned };
enum class HostLinkDirection { Command, HostToDevice, DeviceToHost };
enum class SimulationPhase { Initialize, Warmup, Measure, Drain };
enum class SimulationProfile { MediaResearch, HbfV07, AiSystem };
enum class ProtocolAbstraction { Transaction, Flit };
enum class ChannelMediaPolicy { Linear, FineStripe };
enum class HbfCompletionClass {
  Success,
  SuccessWithAdvisory,
  RetryRequired,
  Failed,
};
enum class HbfStatus {
  Success,
  InvalidAddress,
  TemporarilyRestricted,
  InvalidUserField,
  OverlappingAddress,
  MaxPendingDluReached,
  DluAccumulationTimeout,
  WriteOrderViolation,
  ProgramFailure,
  EraseFailure,
  UncorrectableEcc,
  ProgramFailureReplayRequired,
  CorrectedEccRefreshRequired,
  UncorrectableEccRefreshRequired,
  UncorrectableEccRetryRequired,
  ErasedPageRead,
  ReadPendingWrite,
  ReducedCapacity,
  DieTemporarilyBlocked,
  Pending,
};
enum class RetirementGranularity { Block, Bank, Die, Channel };

std::string to_string(OpType op);
std::string to_string(TransactionSource source);
std::string to_string(SimulationProfile profile);
std::string to_string(ProtocolAbstraction abstraction);
std::string to_string(ChannelMediaPolicy policy);
std::string to_string(HbfCompletionClass completion);
std::string to_string(HbfStatus status);
HbfCompletionClass hbf_completion_class(HbfStatus status);
bool hbf_data_valid(HbfStatus status);
std::uint8_t hbf_status_code(OpType op, HbfStatus status);
OpType parse_op(const std::string& value);
std::uint64_t parse_size(const std::string& value);
double parse_bandwidth_bytes_per_ns(const std::string& value);

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

struct PhysicalAddr {
  std::uint32_t stack = 0;
  std::uint32_t die = 0;
  std::uint32_t plane = 0;
  std::uint32_t block = 0;
  std::uint32_t page = 0;
  std::uint64_t offset = 0;
  std::uint32_t data_port = 0;
  std::uint64_t physical_stripe =
      std::numeric_limits<std::uint64_t>::max();
  std::uint32_t generation = 0;
  std::uint32_t channel = 0;
  std::uint32_t bank = 0;
};

struct HbfErrorInfo {
  std::uint64_t logical_address = 0;
  std::optional<PhysicalAddr> physical_address;
  std::optional<std::uint32_t> retry_stage;
  std::optional<RetirementGranularity> retirement_granularity;
  std::string reason;
};

struct HbfResponse {
  std::uint64_t request_id = 0;
  HbfStatus status = HbfStatus::Success;
  HbfCompletionClass completion_class = HbfCompletionClass::Success;
  std::optional<std::uint8_t> protocol_status_code;
  SimTime completion_time = 0;
  std::uint64_t bytes_completed = 0;
  std::optional<HbfErrorInfo> error;

  [[nodiscard]] bool ok() const {
    return completion_class == HbfCompletionClass::Success ||
           completion_class == HbfCompletionClass::SuccessWithAdvisory;
  }
  [[nodiscard]] bool data_valid() const { return hbf_data_valid(status); }
  static HbfResponse success(std::uint64_t request_id,
                             SimTime completion_time = 0,
                             std::uint64_t bytes_completed = 0);
  static HbfResponse failure(std::uint64_t request_id, HbfStatus status,
                             HbfErrorInfo error,
                             SimTime completion_time = 0,
                             std::uint64_t bytes_completed = 0);
};

struct HbfChannelAddress {
  std::uint32_t channel = 0;
  std::uint64_t local_address = 0;
  std::uint32_t axi_port = 0;
  std::uint64_t axi_port_local_address = 0;
};

class HbfChannelDomain {
 public:
  explicit HbfChannelDomain(const Config& config);
  HbfChannelAddress translate(std::uint64_t global_address) const;
  std::uint64_t global_address(const HbfChannelAddress& address) const;
  std::uint32_t channel_count() const { return channel_count_; }
  std::uint64_t channel_capacity() const { return channel_capacity_; }
  std::uint64_t total_capacity() const { return total_capacity_; }
  std::uint64_t interleave() const { return interleave_; }
  std::uint32_t axi_ports_per_channel() const {
    return axi_ports_per_channel_;
  }
  std::uint64_t axi_port_interleave() const {
    return axi_port_interleave_;
  }

 private:
  std::uint32_t channel_count_ = 0;
  std::uint64_t channel_capacity_ = 0;
  std::uint64_t total_capacity_ = 0;
  std::uint64_t interleave_ = 0;
  std::uint32_t axi_ports_per_channel_ = 0;
  std::uint64_t axi_port_interleave_ = 0;
};

struct AxiEndpoint {
  std::uint32_t channel = 0;
  std::uint32_t port = 0;
  std::uint32_t id = 0;
};

class AxiOrderTracker {
 public:
  explicit AxiOrderTracker(const Config& config);
  HbfStatus issue(const AxiEndpoint& endpoint, std::uint64_t request_id);
  std::vector<HbfResponse> complete(HbfResponse response);
  std::size_t outstanding(const AxiEndpoint& endpoint) const;
  std::size_t total_outstanding() const { return owners_.size(); }

 private:
  struct EndpointKey {
    std::uint32_t channel = 0;
    std::uint32_t port = 0;
    std::uint32_t id = 0;
    friend bool operator==(const EndpointKey&, const EndpointKey&) = default;
  };
  struct EndpointHash {
    std::size_t operator()(const EndpointKey& key) const;
  };
  std::uint32_t channel_count_ = 0;
  std::uint32_t ports_per_channel_ = 0;
  std::uint32_t id_count_ = 0;
  std::uint32_t max_outstanding_per_id_ = 0;
  std::unordered_map<EndpointKey, std::deque<std::uint64_t>, EndpointHash>
      issued_;
  std::unordered_map<std::uint64_t, EndpointKey> owners_;
  std::unordered_map<std::uint64_t, HbfResponse> completed_;
};

struct HbfDlu {
  struct Timing {
    SimTime first_fragment_arrival = 0;
    SimTime last_fragment_arrival = 0;
    SimTime total_h2d_wait_ns = 0;
    SimTime total_h2d_service_ns = 0;
    std::uint32_t fragment_count = 0;

    SimTime assembly_latency_ns() const {
      return last_fragment_arrival - first_fragment_arrival;
    }
  };
  HbfChannelAddress address;
  std::uint64_t size = 0;
  std::vector<std::uint64_t> request_ids;
  Timing timing;
};

struct DluAssemblyResult {
  HbfStatus status = HbfStatus::Pending;
  std::optional<HbfDlu> completed;
  std::optional<SimTime> deadline;
};

struct ExpiredDlu {
  HbfChannelAddress address;
  std::vector<std::uint64_t> request_ids;
  HbfStatus status = HbfStatus::DluAccumulationTimeout;
  HbfDlu::Timing timing;
};

enum class DluReadDisposition { NotPending, Forwarded, PendingWrite };

struct DluReadResult {
  DluReadDisposition disposition = DluReadDisposition::NotPending;
  HbfStatus status = HbfStatus::Success;
  SimTime ready_at = 0;
};

class DluAssembler {
 public:
  explicit DluAssembler(const Config& config);
  DluAssemblyResult submit(std::uint64_t request_id,
                           const HbfChannelAddress& address,
                           std::uint64_t bytes, SimTime now,
                           std::optional<SimTime> data_ready_at = std::nullopt,
                           SimTime h2d_wait_ns = 0,
                           SimTime h2d_service_ns = 0);
  std::vector<ExpiredDlu> expire(SimTime now);
  DluReadResult lookup(const HbfChannelAddress& address,
                       std::uint64_t bytes) const;
  std::size_t pending_count() const { return pending_.size(); }
  std::uint64_t dlu_size() const { return dlu_size_; }

 private:
  struct DluKey {
    std::uint32_t channel = 0;
    std::uint64_t local_base = 0;
    friend bool operator==(const DluKey&, const DluKey&) = default;
  };
  struct DluKeyHash {
    std::size_t operator()(const DluKey& key) const;
  };
  struct PendingDlu {
    std::uint64_t generation = 0;
    SimTime deadline = 0;
    std::uint64_t covered_bytes = 0;
    std::vector<std::uint64_t> coverage;
    std::vector<SimTime> fragment_ready_at;
    std::vector<std::uint64_t> request_ids;
    HbfDlu::Timing timing;
  };
  struct DeadlineEntry {
    SimTime deadline = 0;
    DluKey key;
    std::uint64_t generation = 0;
  };
  struct DeadlineCompare {
    bool operator()(const DeadlineEntry& left,
                    const DeadlineEntry& right) const {
      if (left.deadline != right.deadline)
        return left.deadline > right.deadline;
      if (left.key.channel != right.key.channel)
        return left.key.channel > right.key.channel;
      return left.key.local_base > right.key.local_base;
    }
  };
  std::uint64_t dlu_size_ = 0;
  std::uint32_t max_pending_dlus_ = 0;
  SimTime timeout_ns_ = 0;
  std::unordered_map<DluKey, PendingDlu, DluKeyHash> pending_;
  std::unordered_map<std::uint32_t, std::size_t> pending_per_channel_;
  std::priority_queue<DeadlineEntry, std::vector<DeadlineEntry>,
                      DeadlineCompare>
      deadlines_;
  std::uint64_t next_generation_ = 1;
};

struct StripeId {
  std::uint64_t physical_id = std::numeric_limits<std::uint64_t>::max();
  std::uint32_t generation = 0;

  bool valid() const {
    return physical_id != std::numeric_limits<std::uint64_t>::max() &&
           generation != 0;
  }
  friend bool operator==(const StripeId&, const StripeId&) = default;
};

class LazyBitmap {
 public:
  bool test(std::uint32_t bit) const;
  void set(std::uint32_t bit, std::uint32_t bit_count);
  void clear(std::uint32_t bit);
  bool any() const;
  std::uint32_t count() const;
  void reset() { words_.clear(); }
  std::size_t allocated_words() const { return words_.size(); }

 private:
  std::vector<std::uint64_t> words_;
};

struct ExtentRun {
  std::uint32_t physical_start_slot = 0;
  std::uint32_t slot_count = 0;
  std::uint64_t logical_base_lpn = 0;
};

struct StripeDescriptor {
  StripeId id;
  std::uint64_t logical_base_lpn = 0;
  std::uint32_t next_program_slot = 0;
  std::uint32_t valid_slots = 0;
  std::uint32_t reserved_programs = 0;
  std::uint32_t erased_lanes = 0;
  SimTime retention_since = 0;
  StripeState state = StripeState::Free;
  LazyBitmap valid_bitmap;
  LazyBitmap invalid_bitmap;
  LazyBitmap failed_bitmap;
  LazyBitmap hole_bitmap;
  LazyBitmap reserved_bitmap;
  LazyBitmap erased_lane_bitmap;
  std::vector<ExtentRun> extent_runs;
  std::unordered_map<std::uint32_t, std::uint64_t> exceptions;
};

struct ProgramFailureNotice {
  StripeId stripe;
  std::uint32_t failed_slot = 0;
  PhysicalAddr failed_ppa;
  std::uint32_t committed_slots = 0;
};

class StripeMappingTable {
 public:
  explicit StripeMappingTable(const Config& config);

  std::uint32_t stripe_width() const { return stripe_width_; }
  std::uint32_t stripe_capacity() const { return stripe_capacity_; }
  std::uint32_t parallelism_group_count() const {
    return parallelism_group_count_;
  }
  std::uint32_t parallelism_group(const StripeId& stripe) const;
  StripeId allocate(std::uint64_t logical_base_lpn);
  StripeId allocate_replacement(std::uint64_t logical_base_lpn);
  PhysicalAddr preview_program(std::uint64_t lpn) const;
  PhysicalAddr reserve_program(std::uint64_t lpn);
  PhysicalAddr reserve_program(const StripeId& destination,
                               std::uint64_t lpn);
  void reserve_hole(const StripeId& destination, std::uint64_t lpn);
  void commit_program(std::uint64_t lpn, const PhysicalAddr& paddr,
                      SimTime now = 0);
  ProgramFailureNotice fail_program(std::uint64_t lpn,
                                    const PhysicalAddr& paddr);
  void invalidate(std::uint64_t lpn);
  std::optional<PhysicalAddr> lookup(std::uint64_t lpn) const;
  std::optional<std::uint64_t> reverse_lookup(
      const PhysicalAddr& paddr, std::uint32_t expected_generation) const;
  PhysicalAddr address_for(const StripeId& stripe,
                           std::uint32_t slot) const;
  std::uint32_t slot_of(const PhysicalAddr& paddr) const;
  void seal(const StripeId& stripe);
  void begin_migration(const StripeId& source);
  void remap_commit(const StripeId& source, const StripeId& destination);
  void abort_migration(const StripeId& destination);
  void on_erase(const PhysicalAddr& block_addr);
  bool retire_stripe(const PhysicalAddr& block_addr);
  bool validate_generation(const PhysicalAddr& paddr) const;
  const StripeDescriptor& descriptor(const StripeId& stripe) const;
  std::optional<StripeId> active_stripe(std::uint64_t lpn) const;
  std::size_t active_mapping_count() const { return active_.size(); }
  std::size_t free_stripe_count() const { return free_stripes_.size(); }
  std::size_t total_stripe_count() const { return descriptors_.size(); }
  std::size_t usable_stripe_count() const { return usable_stripes_; }
  std::size_t host_visible_stripe_count() const {
    return host_visible_stripes_;
  }
  std::vector<StripeId> active_stripes() const;

 private:
  StripeId allocate_internal(std::uint64_t logical_base_lpn,
                             bool publish);
  StripeDescriptor& mutable_descriptor(const StripeId& stripe);
  std::uint64_t logical_base(std::uint64_t lpn) const;
  PhysicalAddr preview_for(const StripeId& stripe, std::uint64_t lpn) const;
  PhysicalAddr address_from_geometry(std::uint64_t physical,
                                     std::uint32_t generation,
                                     std::uint32_t slot) const;
  const Config& config_;
  std::uint32_t stripe_width_ = 0;
  std::uint32_t stripe_capacity_ = 0;
  std::uint32_t parallelism_group_count_ = 0;
  std::vector<StripeDescriptor> descriptors_;
  std::vector<std::uint32_t> generations_;
  std::deque<std::uint64_t> free_stripes_;
  std::map<std::uint64_t, StripeId> active_;
  std::size_t host_visible_stripes_ = 0;
  std::size_t usable_stripes_ = 0;
};

struct HostGcDecision {
  StripeId victim;
  bool erase_only = false;
};

struct HostGcPollResult {
  std::optional<HostGcDecision> decision;
  std::size_t free_stripes = 0;
  std::size_t low_watermark = 0;
  std::size_t high_watermark = 0;
  bool cycle_started = false;
  bool high_watermark_reached = false;
  bool stalled = false;
};

class HostGcManager {
 public:
  explicit HostGcManager(const Config& config);
  HostGcPollResult poll(const StripeMappingTable& mapping,
                        bool copy_engine_busy);
  void notify_media_change() { ++media_epoch_; }
  bool pressure_active() const { return pressure_active_; }

 private:
  const Config& config_;
  bool pressure_active_ = false;
  std::uint64_t media_epoch_ = 0;
  std::optional<std::uint64_t> stalled_epoch_;
  std::optional<std::size_t> stalled_free_stripes_;
};

struct RefreshDecision {
  StripeId source;
  SimTime deadline = 0;
};

struct RefreshPollResult {
  std::optional<RefreshDecision> decision;
  std::optional<SimTime> next_check_at;
  bool deadline_missed = false;
  bool deferred_no_space = false;
};

class RefreshManager {
 public:
  explicit RefreshManager(const Config& config) : config_(config) {}
  RefreshPollResult poll(const StripeMappingTable& mapping, SimTime now,
                         std::size_t active_refresh_jobs) const;

 private:
  const Config& config_;
};

struct HostRoute {
  std::uint32_t stack = 0;
  std::uint32_t channel = 0;
  std::uint32_t global_channel = 0;
  std::uint64_t channel_local_address = 0;
  std::uint32_t axi_port = 0;
  std::uint64_t axi_port_local_address = 0;
};

struct Request {
  std::uint64_t id = 0;
  SimTime arrival_time = 0;
  OpType op = OpType::Read;
  std::uint64_t logical_addr = 0;
  std::uint64_t size = 0;
  std::uint32_t stream_id = 0;
  SimTime dlu_data_ready = 0;
  std::vector<std::uint64_t> dlu_request_ids;
  HostRoute host_route;
  std::uint32_t pending_subreqs = 0;
  SimTime complete_time = 0;
  SimTime host_command_wait_ns = 0;
  SimTime host_command_service_ns = 0;
  bool measured = true;
  bool failed = false;
  bool internal = false;
  bool axi_tracked = false;
  HbfStatus status = HbfStatus::Success;
  AxiEndpoint axi;
  TransactionSource source = TransactionSource::User;
};

struct LatencyBreakdown {
  SimTime host_command_wait_ns = 0;
  SimTime host_command_service_ns = 0;
  SimTime host_data_wait_ns = 0;
  SimTime host_data_service_ns = 0;
  SimTime nand_queue_wait_ns = 0;
  SimTime nand_command_wait_ns = 0;
  SimTime array_service_ns = 0;
  SimTime auto_erase_service_ns = 0;
  SimTime fabric_wait_ns = 0;
  SimTime fabric_service_ns = 0;
};

struct SubRequest {
  std::uint64_t id = 0;
  std::uint64_t parent_id = 0;
  OpType op = OpType::Read;
  TransactionSource source = TransactionSource::User;
  std::uint64_t lpn = 0;
  std::uint64_t bytes = 0;
  PhysicalAddr paddr;
  std::optional<PhysicalAddr> old_paddr;
  std::optional<std::uint64_t> copy_job_id;
  std::optional<std::uint32_t> copy_slot;
  SimTime arrival_time = 0;
  SimTime enqueue_time = 0;
  SimTime issue_time = 0;
  SimTime complete_time = 0;
  SimTime ready_time = 0;
  SimTime array_active_since = 0;
  SimTime array_completion_time = 0;
  SimTime suspended_remaining_ns = 0;
  std::uint32_t read_attempts = 0;
  HostRoute host_route;
  LatencyBreakdown latency;
  bool suspended = false;
  bool failed = false;
  bool critical = false;
  bool page0_auto_erase = false;
  bool auto_erase_failed = false;
  bool auto_erase_retired = false;
  HbfStatus status = HbfStatus::Success;
};

using FlashTransaction = SubRequest;

struct TraceEntry {
  SimTime timestamp_ns;
  OpType op;
  std::uint64_t address;
  std::uint64_t size;
  std::uint32_t stream;
  std::uint32_t axi_id = 0;
  std::uint32_t axi_port = std::numeric_limits<std::uint32_t>::max();
};

struct HbfValidationResult {
  HbfStatus status = HbfStatus::Success;
  std::string reason;
  std::optional<HbfChannelAddress> address;

  [[nodiscard]] bool ok() const { return status == HbfStatus::Success; }
};

class HbfProtocolValidator {
 public:
  HbfProtocolValidator(const Config& config,
                       const HbfChannelDomain& channels)
      : config_(config), channels_(channels) {}
  HbfValidationResult validate(const TraceEntry& entry) const;

 private:
  const Config& config_;
  const HbfChannelDomain& channels_;
};

class IRequestSource {
 public:
  virtual ~IRequestSource() = default;
  virtual bool next(TraceEntry& entry) = 0;
};

class CsvTraceSource final : public IRequestSource {
 public:
  explicit CsvTraceSource(const std::string& path);
  bool next(TraceEntry& entry) override;

 private:
  std::ifstream input_;
  std::string path_;
  std::uint64_t line_number_ = 0;
  bool first_record_ = true;
  std::optional<SimTime> previous_timestamp_;
};

class AddressMapper {
 public:
  explicit AddressMapper(const Config& config);
  AddressMapper(const Config& config, const HbfChannelDomain& channels);
  PhysicalAddr placement(std::uint64_t lpn) const;
  PhysicalAddr preview_write(std::uint64_t lpn) const;
  PhysicalAddr map_read(std::uint64_t lpn) const;
  PhysicalAddr prepare_write(std::uint64_t lpn);
  PhysicalAddr map_channel_read(const HbfChannelAddress& address) const;
  PhysicalAddr prepare_channel_write(const HbfChannelAddress& address);
  void commit_write(std::uint64_t lpn, const PhysicalAddr& paddr,
                    SimTime now = 0);
  ProgramFailureNotice fail_write(std::uint64_t lpn,
                                  const PhysicalAddr& paddr);
  std::optional<PhysicalAddr> lookup(std::uint64_t lpn) const;
  void on_erase(const PhysicalAddr& block_addr);
  std::uint32_t flat_plane(const PhysicalAddr& addr) const;
  StripeMappingTable* stripe_mapping() { return stripes_.get(); }
  const StripeMappingTable* stripe_mapping() const { return stripes_.get(); }
  bool validate_generation(const PhysicalAddr& paddr) const;
  const HbfChannelDomain& channels() const { return *channels_; }

 private:
  PhysicalAddr base_map(std::uint64_t lpn) const;
  PhysicalAddr base_map_channel(const HbfChannelAddress& address) const;
  const Config& config_;
  std::unique_ptr<HbfChannelDomain> owned_channels_;
  const HbfChannelDomain* channels_ = nullptr;
  std::unique_ptr<StripeMappingTable> stripes_;
};

class LinkResource {
 public:
  LinkResource() = default;
  LinkResource(double bytes_per_ns, SimTime fixed_latency_ns)
      : bytes_per_ns_(bytes_per_ns), fixed_latency_ns_(fixed_latency_ns) {}
  struct Reservation {
    SimTime requested_at = 0;
    SimTime start = 0;
    SimTime transfer_end = 0;
    SimTime completion = 0;
  };
  Reservation reserve_window(SimTime now, std::uint64_t bytes);
  SimTime reserve(SimTime now, std::uint64_t bytes) {
    return reserve_window(now, bytes).completion;
  }
  SimTime free_at() const { return free_at_; }

 private:
  SimTime free_at_ = 0;
  double bytes_per_ns_ = 1.0;
  SimTime fixed_latency_ns_ = 0;
};

class DataFabric {
 public:
  DataFabric() = default;
  DataFabric(std::uint32_t ports, double aggregate_bytes_per_ns,
             double port_bytes_per_ns, SimTime fixed_latency_ns);
  LinkResource::Reservation reserve_window(SimTime now, std::uint64_t bytes,
                                            std::uint32_t port);
  SimTime reserve(SimTime now, std::uint64_t bytes, std::uint32_t port) {
    return reserve_window(now, bytes, port).completion;
  }

 private:
  std::vector<SimTime> port_free_at_;
  SimTime aggregate_free_at_ = 0;
  double aggregate_bytes_per_ns_ = 1.0;
  double port_bytes_per_ns_ = 1.0;
  SimTime fixed_latency_ns_ = 0;
};

class HostRouter {
 public:
  explicit HostRouter(const Config& config);
  HostRouter(const Config& config, const HbfChannelDomain& channels);
  HostRoute route(std::uint64_t logical_addr,
                  const PhysicalAddr& media_address) const;
  HbfChannelAddress channel_address(std::uint64_t logical_addr) const {
    return channels_->translate(logical_addr);
  }
  const HbfChannelDomain& channels() const { return *channels_; }

 private:
  const Config& config_;
  std::unique_ptr<HbfChannelDomain> owned_channels_;
  const HbfChannelDomain* channels_ = nullptr;
};

class HostInterface {
 public:
  HostInterface() = default;
  HostInterface(std::uint32_t channels, double bytes_per_ns,
                SimTime fixed_latency_ns, bool full_duplex);
  LinkResource::Reservation reserve(const HostRoute& route,
                                    HostLinkDirection direction,
                                    SimTime now, std::uint64_t bytes);

 private:
  struct Channel {
    LinkResource shared;
    LinkResource command;
    LinkResource host_to_device;
    LinkResource device_to_host;
  };
  bool full_duplex_ = true;
  std::vector<Channel> channels_;
};

struct ReadErrorResult {
  ReadErrorStatus status = ReadErrorStatus::Clean;
  std::uint64_t bit_errors = 0;
};

class ReliabilityModel {
 public:
  explicit ReliabilityModel(const Config& config);
  bool program_failed(std::uint32_t erase_count = 0);
  bool erase_failed(std::uint32_t erase_count = 0);
  ReadErrorResult read_result(std::uint64_t bytes, std::uint32_t retry,
                              std::uint32_t erase_count = 0);

 private:
  const Config& config_;
  std::mt19937_64 random_;
  std::uint64_t injected_program_failures_ = 0;
};

struct HbfSystemCapabilities {
  bool spec_profile = false;
  bool ai_system_semantics = false;
  bool transaction_protocol = true;
  bool research_stripe_mapping = true;
  bool research_copy_gc = true;
  bool research_migration_recovery = true;
};

// Composition root for the HBF device model. Simulator owns time and events;
// HbfSystem owns address, routing, reliability, and maintenance services.
class HbfSystem {
 public:
  explicit HbfSystem(const Config& config);

  SimulationProfile profile() const { return profile_; }
  ProtocolAbstraction protocol_abstraction() const {
    return protocol_abstraction_;
  }
  const HbfSystemCapabilities& capabilities() const { return capabilities_; }
  AddressMapper& mapper() { return mapper_; }
  const AddressMapper& mapper() const { return mapper_; }
  HostRouter& host_router() { return host_router_; }
  const HostRouter& host_router() const { return host_router_; }
  const HbfChannelDomain& channels() const { return channels_; }
  const HbfProtocolValidator& protocol_validator() const {
    return protocol_validator_;
  }
  AxiOrderTracker& axi() { return axi_; }
  const AxiOrderTracker& axi() const { return axi_; }
  DluAssembler& dlu_assembler() { return dlu_assembler_; }
  const DluAssembler& dlu_assembler() const { return dlu_assembler_; }
  ReliabilityModel& reliability() { return reliability_; }
  const ReliabilityModel& reliability() const { return reliability_; }
  HostGcManager& host_gc_manager() { return host_gc_manager_; }
  const HostGcManager& host_gc_manager() const { return host_gc_manager_; }
  RefreshManager& refresh_manager() { return refresh_manager_; }
  const RefreshManager& refresh_manager() const { return refresh_manager_; }

 private:
  SimulationProfile profile_ = SimulationProfile::MediaResearch;
  ProtocolAbstraction protocol_abstraction_ =
      ProtocolAbstraction::Transaction;
  HbfSystemCapabilities capabilities_;
  HbfChannelDomain channels_;
  AddressMapper mapper_;
  HostRouter host_router_;
  HbfProtocolValidator protocol_validator_;
  AxiOrderTracker axi_;
  DluAssembler dlu_assembler_;
  ReliabilityModel reliability_;
  HostGcManager host_gc_manager_;
  RefreshManager refresh_manager_;
};

struct BlockMeta {
  BlockState state = BlockState::Free;
  std::uint32_t erase_count = 0;
  std::uint32_t next_program_page = 0;
  std::uint32_t valid_pages = 0;
  std::uint32_t invalid_pages = 0;
  bool bad = false;
  SimTime last_program_time = 0;
  SimTime last_refresh_time = 0;
  SimTime ready_at = 0;
  std::vector<std::uint64_t> valid_bitmap;
  std::vector<std::uint64_t> invalid_bitmap;
  std::vector<std::uint64_t> failed_bitmap;
};

struct DieState {
  SimTime ready_at = 0;
  SimTime command_ready_at = 0;
};

struct ReadCacheEntry {
  bool valid = false;
  PhysicalAddr page;
  SimTime ready_at = 0;
  std::uint64_t last_use = 0;
};

struct BankState {
  SimTime command_ready_at = 0;
  std::vector<ReadCacheEntry> read_cache;
};

struct Plane {
  static constexpr std::size_t kSourceCount = 6;
  using SourceQueues =
      std::array<std::deque<std::uint64_t>, kSourceCount>;
  bool busy = false;
  SourceQueues reads;
  SourceQueues writes;
  SourceQueues erases;
  SourceQueues refreshes;
  std::uint32_t consecutive_reads = 0;
  SimTime ready_at = 0;
  bool data_register_busy = false;
  bool suspend_pending = false;
  std::optional<std::uint64_t> active_subrequest;
  std::optional<std::uint64_t> suspended_subrequest;
  std::optional<std::uint64_t> cached_write;
  std::vector<BlockMeta> blocks;
};

enum class EventType { DispatchWake, RefreshManagerWake, DluTimeout,
                       ResourceFabricStart, ResourceFabricEnd,
                       ResourceHostStart, ResourceHostEnd,
                       HostArrival, HostCommandDone, SubreqReady,
                       NandSuspendDone, NandReadDone,
                       NandDataInDone, NandAutoEraseDone,
                       NandAutoEraseProgramReady, NandProgramDone,
                       NandEraseDone,
                       NandRefreshDone, NandDataOutDone,
                       ReadCacheDataOutDone, SubreqDone };
struct Event {
  SimTime time;
  std::uint64_t seq;
  EventType type;
  std::uint64_t request_id;
  std::uint64_t subreq_id;
};
struct EventCompare {
  bool operator()(const Event& a, const Event& b) const {
    return a.time != b.time ? a.time > b.time : a.seq > b.seq;
  }
};

class EventQueue {
 public:
  void schedule(SimTime when, EventType type, std::uint64_t request_id,
                std::uint64_t subrequest_id = 0);
  bool empty() const { return events_.empty(); }
  const Event& next() const { return events_.top(); }
  Event pop();

 private:
  std::uint64_t next_sequence_ = 0;
  std::priority_queue<Event, std::vector<Event>, EventCompare> events_;
};

class LatencyHistogram {
 public:
  void record(SimTime value);
  std::uint64_t count() const { return count_; }
  double mean() const;
  double percentile(double quantile) const;

 private:
  static constexpr std::size_t kLinearBuckets = 1024;
  static constexpr std::size_t kSubBuckets = 64;
  static constexpr std::size_t kExponentBuckets = 54;
  static constexpr std::size_t kBucketCount =
      kLinearBuckets + kSubBuckets * kExponentBuckets;
  std::array<std::uint64_t, kBucketCount> buckets_{};
  std::uint64_t count_ = 0;
  long double sum_ = 0;
};

enum class ResourceKind { Array, Fabric, Host };

class ResourceTracker {
 public:
  void configure(std::uint32_t stacks, std::uint32_t dies_per_stack,
                 std::uint32_t planes_per_die,
                 std::uint32_t ports_per_stack,
                 std::uint32_t host_channels_per_stack);
  void transition(ResourceKind kind, std::uint32_t stack,
                  std::uint32_t local_index, int delta, SimTime now);
  SimTime array_busy(std::uint32_t stack, SimTime until) const;
  SimTime fabric_busy(std::uint32_t stack, SimTime until) const;
  SimTime host_busy(std::uint32_t stack, SimTime until) const;
  SimTime array_fabric_overlap(std::uint32_t stack, SimTime until) const;
  long double active_plane_area(std::uint32_t stack, SimTime until) const;
  std::uint32_t max_active_planes(std::uint32_t stack) const;
  SimTime plane_busy(std::uint32_t flat_plane, SimTime until) const;
  SimTime die_busy(std::uint32_t flat_die, SimTime until) const;
  SimTime port_busy(std::uint32_t flat_port, SimTime until) const;
  SimTime host_channel_busy(std::uint32_t flat_channel,
                            SimTime until) const;

 private:
  struct BusyCounter {
    SimTime last_time = 0;
    SimTime busy_ns = 0;
    std::uint32_t active = 0;
    std::uint32_t maximum = 0;
  };
  struct StackCounter {
    SimTime last_time = 0;
    SimTime array_busy_ns = 0;
    SimTime fabric_busy_ns = 0;
    SimTime host_busy_ns = 0;
    SimTime overlap_ns = 0;
    long double active_plane_area_ns = 0;
    std::uint32_t arrays = 0;
    std::uint32_t fabrics = 0;
    std::uint32_t hosts = 0;
    std::uint32_t max_arrays = 0;
  };
  static SimTime busy_until(const BusyCounter& counter, SimTime until);
  static void advance(BusyCounter& counter, int delta, SimTime now);
  static void advance(StackCounter& counter, ResourceKind kind, int delta,
                      SimTime now);
  std::uint32_t dies_per_stack_ = 0;
  std::uint32_t planes_per_die_ = 0;
  std::uint32_t ports_per_stack_ = 0;
  std::uint32_t host_channels_per_stack_ = 0;
  std::vector<StackCounter> stacks_;
  std::vector<BusyCounter> planes_;
  std::vector<BusyCounter> dies_;
  std::vector<BusyCounter> ports_;
  std::vector<BusyCounter> host_channels_;
};

class StatsCollector {
 public:
  void record_request(const Request& request);
  void record_subrequest(const SubRequest& subrequest);
  void record_resource_transition(ResourceKind kind, std::uint32_t stack,
                                  std::uint32_t local_index, int delta,
                                  SimTime now) {
    resources_.transition(kind, stack, local_index, delta, now);
  }
  void record_queue_depth(SimTime time, std::uint64_t reads,
                          std::uint64_t writes, std::uint64_t erases,
                          std::uint64_t refreshes,
                          std::uint64_t active_planes);
  void record_read_retry() { ++read_retries_; }
  void record_corrected_read() { ++corrected_reads_; }
  void record_uncorrectable_read() { ++uncorrectable_reads_; }
  void record_program_failure() {
    ++program_failures_;
    ++program_failure_notices_;
  }
  void record_erase_failure() { ++erase_failures_; }
  void record_page0_auto_erase() { ++page0_auto_erases_; }
  void record_retired_block() { ++retired_blocks_; }
  void record_capacity_loss(std::uint64_t capacity_loss_bytes) {
    usable_physical_capacity_bytes_ =
        capacity_loss_bytes > usable_physical_capacity_bytes_
            ? 0
            : usable_physical_capacity_bytes_ - capacity_loss_bytes;
  }
  void record_retired_stripe(std::uint64_t capacity_loss_bytes) {
    ++retired_stripes_;
    record_capacity_loss(capacity_loss_bytes);
  }
  void set_capacity(std::uint64_t physical_bytes,
                    std::uint64_t host_visible_bytes) {
    total_physical_capacity_bytes_ = physical_bytes;
    usable_physical_capacity_bytes_ = physical_bytes;
    host_visible_capacity_bytes_ = host_visible_bytes;
  }
  void set_stripe_geometry(std::uint32_t groups, std::uint32_t width,
                           std::uint32_t capacity) {
    parallelism_groups_ = groups;
    stripe_width_pages_ = width;
    stripe_capacity_pages_ = capacity;
  }
  void record_remap_commit(TransactionSource source, SimTime latency_ns);
  void record_aborted_migration() { ++aborted_migrations_; }
  void record_copy_job(TransactionSource source, bool failed);
  void record_copy_buffer_high_watermark(TransactionSource source,
                                         std::uint64_t bytes);
  void observe_free_stripes(std::uint64_t free_stripes,
                            std::uint64_t host_visible_stripes);
  void record_host_gc_cycle_started() { ++host_gc_cycles_started_; }
  void record_host_gc_high_watermark() {
    ++host_gc_high_watermark_reached_;
  }
  void record_host_gc_stall() { ++host_gc_stalls_; }
  void record_automatic_gc_job(bool erase_only) {
    ++automatic_gc_jobs_;
    if (erase_only) ++automatic_gc_erase_only_jobs_;
  }
  void record_automatic_refresh_job(bool deadline_missed) {
    ++automatic_refresh_jobs_;
    if (deadline_missed) ++refresh_deadline_misses_;
  }
  void record_refresh_deferred() { ++refresh_deferred_no_space_; }
  void record_dlu_completed(const HbfDlu::Timing& timing);
  void record_dlu_timeout() { ++dlu_timeouts_; }
  void record_dlu_rejection(HbfStatus status);
  void record_dlu_forwarded(std::uint64_t bytes) {
    ++dlu_forwarded_reads_;
    dlu_forwarded_bytes_ += bytes;
  }
  void record_dlu_pending_read() { ++dlu_pending_reads_; }
  void record_read_cache_hit(std::uint64_t bytes) {
    ++read_cache_hits_;
    read_cache_hit_bytes_ += bytes;
  }
  void record_read_cache_miss() { ++read_cache_misses_; }
  void record_read_cache_eviction() { ++read_cache_evictions_; }
  void set_topology(std::uint32_t stacks, std::uint32_t dies_per_stack,
                    std::uint32_t planes_per_die,
                    std::uint32_t ports_per_stack,
                    std::uint32_t host_channels_per_stack);
  void set_queue_depth_sample_interval(SimTime interval_ns) {
    queue_depth_sample_interval_ns_ = interval_ns;
  }
  void write(const std::string& output_dir, SimTime makespan) const;
  std::uint64_t completed_requests() const { return completed_requests_; }
  double mean_latency_ns() const;
  double p99_latency_ns() const;
  double percentile_latency_ns(double quantile) const;
  std::uint64_t failed_requests() const { return failed_requests_; }
  std::uint64_t advisory_requests() const { return advisory_requests_; }
  std::uint64_t retry_required_requests() const {
    return retry_required_requests_;
  }
  std::uint64_t read_cache_hits() const { return read_cache_hits_; }
  std::uint64_t read_cache_misses() const { return read_cache_misses_; }
  std::uint64_t read_cache_evictions() const {
    return read_cache_evictions_;
  }
  std::uint64_t read_retries() const { return read_retries_; }
  std::uint64_t corrected_reads() const { return corrected_reads_; }
  std::uint64_t uncorrectable_reads() const { return uncorrectable_reads_; }
  std::uint64_t program_failures() const { return program_failures_; }
  std::uint64_t erase_failures() const { return erase_failures_; }
  std::uint64_t retired_blocks() const { return retired_blocks_; }
  std::uint64_t retired_stripes() const { return retired_stripes_; }
  std::uint64_t usable_physical_capacity_bytes() const {
    return usable_physical_capacity_bytes_;
  }
  std::uint64_t program_failure_notices() const {
    return program_failure_notices_;
  }
  std::uint64_t source_bytes(TransactionSource source, OpType op) const;
  std::uint64_t remap_commits() const { return remap_commits_; }
  std::uint64_t aborted_migrations() const { return aborted_migrations_; }
  std::uint64_t completed_recovery_jobs() const {
    return completed_recovery_jobs_;
  }
  std::uint64_t failed_recovery_jobs() const {
    return failed_recovery_jobs_;
  }
  std::uint64_t completed_gc_jobs() const { return completed_gc_jobs_; }
  std::uint64_t failed_gc_jobs() const { return failed_gc_jobs_; }
  std::uint64_t automatic_gc_jobs() const { return automatic_gc_jobs_; }
  std::uint64_t automatic_gc_erase_only_jobs() const {
    return automatic_gc_erase_only_jobs_;
  }
  std::uint64_t host_gc_stalls() const { return host_gc_stalls_; }
  std::uint64_t host_gc_cycles_started() const {
    return host_gc_cycles_started_;
  }
  std::uint64_t host_gc_high_watermark_reached() const {
    return host_gc_high_watermark_reached_;
  }
  std::uint64_t min_free_stripes() const { return min_free_stripes_; }
  std::uint64_t automatic_refresh_jobs() const {
    return automatic_refresh_jobs_;
  }
  std::uint64_t completed_refresh_jobs() const {
    return completed_refresh_jobs_;
  }
  std::uint64_t failed_refresh_jobs() const {
    return failed_refresh_jobs_;
  }
  std::uint64_t refresh_deadline_misses() const {
    return refresh_deadline_misses_;
  }
  std::uint64_t refresh_deferred_no_space() const {
    return refresh_deferred_no_space_;
  }
  std::uint64_t copy_buffer_high_watermark(
      TransactionSource source) const;
  std::size_t queue_depth_sample_count() const {
    return queue_depth_samples_.size() +
           (latest_queue_depth_ &&
                    (queue_depth_samples_.empty() ||
                     queue_depth_samples_.back().time !=
                         latest_queue_depth_->time)
                ? 1
                : 0);
  }

 private:
  struct QueueDepthSample {
    SimTime time = 0;
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
    std::uint64_t erases = 0;
    std::uint64_t refreshes = 0;
    std::uint64_t active_planes = 0;
  };
  std::uint64_t completed_requests_ = 0;
  std::uint64_t read_requests_ = 0;
  std::uint64_t write_requests_ = 0;
  std::uint64_t completed_bytes_ = 0;
  std::uint64_t successful_bytes_ = 0;
  std::uint64_t failed_requests_ = 0;
  std::uint64_t advisory_requests_ = 0;
  std::uint64_t retry_required_requests_ = 0;
  std::uint64_t read_retries_ = 0;
  std::uint64_t corrected_reads_ = 0;
  std::uint64_t uncorrectable_reads_ = 0;
  std::uint64_t program_failures_ = 0;
  std::uint64_t program_failure_notices_ = 0;
  std::uint64_t erase_failures_ = 0;
  std::uint64_t page0_auto_erases_ = 0;
  std::uint64_t retired_blocks_ = 0;
  std::uint64_t retired_stripes_ = 0;
  std::uint64_t total_physical_capacity_bytes_ = 0;
  std::uint64_t usable_physical_capacity_bytes_ = 0;
  std::uint64_t host_visible_capacity_bytes_ = 0;
  std::uint32_t parallelism_groups_ = 0;
  std::uint32_t stripe_width_pages_ = 0;
  std::uint32_t stripe_capacity_pages_ = 0;
  std::uint64_t remap_commits_ = 0;
  std::uint64_t aborted_migrations_ = 0;
  std::uint64_t completed_recovery_jobs_ = 0;
  std::uint64_t failed_recovery_jobs_ = 0;
  std::uint64_t completed_gc_jobs_ = 0;
  std::uint64_t failed_gc_jobs_ = 0;
  std::uint64_t host_gc_cycles_started_ = 0;
  std::uint64_t host_gc_high_watermark_reached_ = 0;
  std::uint64_t host_gc_stalls_ = 0;
  std::uint64_t automatic_gc_jobs_ = 0;
  std::uint64_t automatic_gc_erase_only_jobs_ = 0;
  std::uint64_t automatic_refresh_jobs_ = 0;
  std::uint64_t completed_refresh_jobs_ = 0;
  std::uint64_t failed_refresh_jobs_ = 0;
  std::uint64_t refresh_deadline_misses_ = 0;
  std::uint64_t refresh_deferred_no_space_ = 0;
  std::uint64_t completed_dlus_ = 0;
  std::uint64_t dlu_timeouts_ = 0;
  std::uint64_t dlu_overlaps_ = 0;
  std::uint64_t dlu_capacity_rejections_ = 0;
  std::uint64_t dlu_forwarded_reads_ = 0;
  std::uint64_t dlu_forwarded_bytes_ = 0;
  std::uint64_t dlu_pending_reads_ = 0;
  std::uint64_t read_cache_hits_ = 0;
  std::uint64_t read_cache_misses_ = 0;
  std::uint64_t read_cache_evictions_ = 0;
  std::uint64_t read_cache_hit_bytes_ = 0;
  SimTime dlu_total_h2d_wait_ns_ = 0;
  SimTime dlu_total_h2d_service_ns_ = 0;
  LatencyHistogram dlu_assembly_latencies_;
  std::uint64_t min_free_stripes_ = 0;
  std::uint64_t host_visible_stripes_ = 0;
  bool observed_free_stripes_ = false;
  std::uint32_t stacks_ = 0;
  std::uint32_t dies_per_stack_ = 0;
  std::uint32_t planes_per_die_ = 0;
  std::uint32_t ports_per_stack_ = 0;
  std::uint32_t host_channels_per_stack_ = 0;
  std::optional<SimTime> first_arrival_;
  SimTime last_completion_ = 0;
  LatencyHistogram latencies_;
  std::map<OpType, LatencyHistogram> op_latencies_;
  std::map<OpType, std::uint64_t> op_bytes_;
  std::map<OpType, LatencyBreakdown> latency_breakdown_;
  std::map<OpType, std::uint64_t> latency_breakdown_samples_;
  std::map<std::pair<TransactionSource, OpType>, LatencyBreakdown>
      source_latency_breakdown_;
  std::map<std::pair<TransactionSource, OpType>, std::uint64_t>
      source_latency_samples_;
  std::map<std::pair<TransactionSource, OpType>, std::uint64_t>
      source_bytes_;
  std::map<std::pair<TransactionSource, OpType>, std::uint64_t>
      source_failures_;
  LatencyHistogram recovery_latencies_;
  LatencyHistogram gc_latencies_;
  LatencyHistogram refresh_latencies_;
  std::map<TransactionSource, std::uint64_t>
      copy_buffer_high_watermarks_;
  ResourceTracker resources_;
  SimTime queue_depth_sample_interval_ns_ = 1'000;
  std::vector<QueueDepthSample> queue_depth_samples_;
  std::optional<QueueDepthSample> latest_queue_depth_;
};

class Simulator {
 public:
  explicit Simulator(Config config);
  void submit(const TraceEntry& entry);
  void run();
  void run(IRequestSource& source);
  void run_until(SimTime until);
  const StatsCollector& stats() const { return stats_; }
  HbfSystem& system() { return system_; }
  const HbfSystem& system() const { return system_; }
  const std::vector<HbfResponse>& responses() const { return responses_; }
  SimTime now() const { return now_; }
  SimulationPhase phase() const { return phase_; }
  PageState page_state(const PhysicalAddr& paddr) const;
  BlockState block_state(const PhysicalAddr& paddr) const;
  SimTime block_ready_at(const PhysicalAddr& paddr) const;
  std::uint32_t block_erase_count(const PhysicalAddr& paddr) const;
  SimTime die_ready_at(const PhysicalAddr& paddr) const;
  const std::vector<ProgramFailureNotice>& program_failure_notices() const {
    return program_failure_notices_;
  }
  std::uint64_t start_host_gc(std::uint64_t logical_addr);
  std::uint64_t start_refresh(std::uint64_t logical_addr);
  void invalidate_host_page(std::uint64_t logical_addr);
  std::size_t active_copy_jobs() const { return copy_jobs_.size(); }

 private:
  struct CopyJob;
  void schedule(SimTime when, EventType type, std::uint64_t request_id,
                std::uint64_t subreq_id = 0);
  void handle(const Event& event);
  void split_request(Request& request);
  void enqueue_subrequest(SubRequest& subrequest);
  void dispatch_stack(std::uint32_t stack, SimTime now);
  void schedule_dispatch_wake(std::uint32_t stack, SimTime when);
  std::optional<std::uint64_t> choose_next(Plane& plane, SimTime now) const;
  void issue(std::uint64_t subrequest_id, SimTime now,
             bool shared_command = false);
  void begin_data_in(std::uint64_t subrequest_id, SimTime now,
                     bool cached);
  void start_program(std::uint64_t subrequest_id, SimTime now,
                     bool shared_command = false);
  void dispatch_ready_programs(std::uint32_t stack, SimTime now);
  bool try_issue_cached_write(Plane& plane, SimTime now);
  bool try_suspend_for_read(Plane& plane, SimTime now);
  bool try_resume(Plane& plane, SimTime now);
  void release_array(const SubRequest& subrequest);
  void finish_program(SubRequest& subrequest, SimTime now);
  LinkResource::Reservation reserve_host(const HostRoute& route,
                                         HostLinkDirection direction,
                                         SimTime now, std::uint64_t bytes,
                                         bool measured);
  LinkResource::Reservation reserve_fabric(const PhysicalAddr& paddr,
                                           SimTime now, std::uint64_t bytes,
                                           bool measured);
  void complete_subrequest(std::uint64_t subrequest_id, SimTime now);
  void publish_response(const Request& request);
  std::uint32_t plane_index(const PhysicalAddr& paddr) const;
  Plane& plane(const PhysicalAddr& paddr);
  const Plane& plane(const PhysicalAddr& paddr) const;
  DieState& die(const PhysicalAddr& paddr);
  const DieState& die(const PhysicalAddr& paddr) const;
  BankState& bank(const PhysicalAddr& paddr);
  const BankState& bank(const PhysicalAddr& paddr) const;
  std::uint32_t bank_index(const PhysicalAddr& paddr) const;
  bool read_cache_lookup(const PhysicalAddr& paddr, SimTime now);
  void read_cache_fill(const PhysicalAddr& paddr, SimTime now,
                       bool measured);
  void invalidate_read_cache_page(const PhysicalAddr& paddr);
  void invalidate_read_cache_block(const PhysicalAddr& paddr);
  SimTime command_ready_time(const SubRequest& subrequest) const;
  void claim_command(const SubRequest& subrequest, SimTime now,
                     bool shared_command);
  std::uint64_t page_key(const PhysicalAddr& paddr) const;
  std::uint64_t block_key(const PhysicalAddr& paddr) const;
  void set_transient_page_state(const PhysicalAddr& paddr, PageState state);
  void clear_transient_page_state(const PhysicalAddr& paddr);
  void materialize_initialized_page(const PhysicalAddr& paddr);
  void retire_block(const PhysicalAddr& paddr);
  bool is_measured(std::uint64_t request_id) const;
  void record_queue_depth();
  void start_array_tracking(const SubRequest& subrequest, SimTime now);
  void stop_array_tracking(const SubRequest& subrequest, SimTime now);
  std::uint64_t start_copy_job(TransactionSource source,
                               const StripeId& stripe,
                               std::optional<std::uint32_t> replay_slot,
                               bool measured, SimTime now);
  std::uint64_t start_gc_job(const StripeId& stripe, bool measured,
                             SimTime now);
  std::uint64_t start_gc_erase_only(const StripeId& stripe, bool measured,
                                    SimTime now);
  void maybe_start_host_gc(SimTime now);
  void maybe_start_automatic_refresh(SimTime now);
  void schedule_refresh_check(SimTime when);
  std::size_t active_copy_jobs(TransactionSource source) const;
  bool has_refresh_horizon(SimTime when) const;
  void advance_copy_job(std::uint64_t job_id, SimTime now);
  void handle_copy_completion(std::uint64_t job_id,
                              std::optional<std::uint32_t> slot,
                              OpType op, bool failed, SimTime now);
  void enqueue_copy_read(std::uint64_t job_id, std::uint32_t slot,
                         SimTime now);
  void enqueue_copy_program(std::uint64_t job_id, std::uint32_t slot,
                            SimTime now);
  void reserve_copy_hole(CopyJob& job, std::uint32_t slot);
  void enqueue_copy_erases(std::uint64_t job_id, SimTime now);
  void enqueue_stripe_erases(const StripeId& stripe,
                             TransactionSource source, bool measured,
                             std::optional<std::uint64_t> copy_job_id,
                             SimTime now);
  void restart_copy_job(std::uint64_t job_id, SimTime now);
  void finish_copy_job(std::uint64_t job_id, SimTime now, bool failed);
  void start_ready_recoveries(SimTime now);
  void reset_copy_attempt(CopyJob& job);
  void handle_copy_failure_drain(std::uint64_t job_id, SimTime now);

  enum class CopyStage { Copying, ErasingSource, CleaningDestination };
  enum class CopySlotStage {
    Reading,
    Buffered,
    ProgrammingBuffered,
    ProgrammingReplay,
  };
  struct CopyJob {
    std::uint64_t id = 0;
    TransactionSource source = TransactionSource::Recovery;
    StripeId source_stripe;
    StripeId destination_stripe;
    std::uint32_t next_read_slot = 0;
    std::uint32_t next_program_slot = 0;
    std::uint32_t slot_limit = 0;
    std::optional<std::uint32_t> replay_slot;
    std::uint32_t attempts = 0;
    std::uint32_t pending_erases = 0;
    std::uint32_t inflight_reads = 0;
    std::uint32_t inflight_programs = 0;
    std::uint64_t buffer_reserved_bytes = 0;
    std::uint64_t buffer_used_bytes = 0;
    std::uint64_t buffer_high_watermark = 0;
    SimTime start_time = 0;
    bool measured = true;
    bool failure_pending = false;
    bool retry_after_drain = false;
    bool erase_only = false;
    CopyStage stage = CopyStage::Copying;
    std::unordered_map<std::uint32_t, CopySlotStage> slots;
  };
  struct PendingRecovery {
    StripeId source_stripe;
    bool measured = true;
  };

  Config config_;
  HbfSystem system_;
  // Transitional aliases keep the event/scheduler implementation stable while
  // ownership moves behind HbfSystem's component boundary.
  AddressMapper& mapper_;
  HostRouter& host_router_;
  ReliabilityModel& reliability_;
  HostGcManager& host_gc_manager_;
  RefreshManager& refresh_manager_;
  SimTime now_ = 0;
  std::uint64_t next_request_id_ = 0;
  std::uint64_t next_subrequest_id_ = 0;
  std::uint64_t submitted_requests_ = 0;
  EventQueue event_queue_;
  SimTime refresh_check_at_ = std::numeric_limits<SimTime>::max();
  std::optional<SimTime> next_trace_arrival_;
  std::unordered_map<std::uint64_t, Request> requests_;
  std::unordered_map<std::uint64_t, SubRequest> subrequests_;
  std::vector<Plane> planes_;
  std::vector<DieState> dies_;
  std::vector<BankState> banks_;
  std::uint64_t read_cache_clock_ = 0;
  std::vector<std::uint32_t> active_per_die_;
  std::vector<std::uint32_t> active_per_stack_;
  std::vector<std::uint32_t> dispatch_cursor_per_stack_;
  std::vector<SimTime> dispatch_wake_at_;
  std::vector<std::deque<std::uint64_t>> program_ready_;
  std::vector<HostInterface> host_interfaces_;
  std::vector<DataFabric> fabrics_;
  std::unordered_map<std::uint64_t, PageState> transient_page_states_;
  std::unordered_set<std::uint64_t> erased_blocks_;
  std::vector<ProgramFailureNotice> program_failure_notices_;
  std::vector<HbfResponse> responses_;
  std::unordered_map<std::uint64_t, CopyJob> copy_jobs_;
  std::vector<PendingRecovery> pending_recoveries_;
  std::uint64_t next_copy_job_id_ = 0;
  std::array<std::uint64_t, 4> queue_depth_{};
  SimulationPhase phase_ = SimulationPhase::Initialize;
  bool streaming_submission_ = false;
  StatsCollector stats_;
};

}  // namespace hbfsim
