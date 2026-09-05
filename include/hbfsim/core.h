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

std::string to_string(OpType op);
std::string to_string(TransactionSource source);
OpType parse_op(const std::string& value);
std::uint64_t parse_size(const std::string& value);
double parse_bandwidth_bytes_per_ns(const std::string& value);

struct Config {
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
  std::uint64_t program_failure_budget = 0;
  double raw_bit_error_rate = 0.0;
  double retry_ber_multiplier = 0.25;
  std::uint32_t ecc_correctable_bits = 0;
  std::uint32_t max_read_retries = 0;
  std::uint64_t random_seed = 1;
  std::uint64_t max_requests = 0;
  std::uint64_t warmup_requests = 0;
  std::string output_dir = "results";

  static Config from_yaml_file(const std::string& path);
  void validate() const;
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
  bool validate_generation(const PhysicalAddr& paddr) const;
  const StripeDescriptor& descriptor(const StripeId& stripe) const;
  std::optional<StripeId> active_stripe(std::uint64_t lpn) const;
  std::size_t active_mapping_count() const { return active_.size(); }
  std::size_t free_stripe_count() const { return free_stripes_.size(); }
  std::size_t total_stripe_count() const { return descriptors_.size(); }
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
  const Config& config_;
  std::uint32_t stripe_width_ = 0;
  std::uint32_t stripe_capacity_ = 0;
  std::vector<StripeDescriptor> descriptors_;
  std::vector<std::uint32_t> generations_;
  std::deque<std::uint64_t> free_stripes_;
  std::map<std::uint64_t, StripeId> active_;
  std::size_t host_visible_stripes_ = 0;
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
};

struct Request {
  std::uint64_t id = 0;
  SimTime arrival_time = 0;
  OpType op = OpType::Read;
  std::uint64_t logical_addr = 0;
  std::uint64_t size = 0;
  std::uint32_t stream_id = 0;
  HostRoute host_route;
  std::uint32_t pending_subreqs = 0;
  SimTime complete_time = 0;
  SimTime host_command_wait_ns = 0;
  SimTime host_command_service_ns = 0;
  bool measured = true;
  bool failed = false;
  bool internal = false;
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
};

using FlashTransaction = SubRequest;

struct TraceEntry {
  SimTime timestamp_ns;
  OpType op;
  std::uint64_t address;
  std::uint64_t size;
  std::uint32_t stream;
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
  PhysicalAddr placement(std::uint64_t lpn) const;
  PhysicalAddr preview_write(std::uint64_t lpn) const;
  PhysicalAddr map_read(std::uint64_t lpn) const;
  PhysicalAddr prepare_write(std::uint64_t lpn);
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

 private:
  PhysicalAddr base_map(std::uint64_t lpn) const;
  const Config& config_;
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
  explicit HostRouter(const Config& config) : config_(config) {}
  HostRoute route(std::uint64_t logical_addr,
                  const PhysicalAddr& media_address) const;

 private:
  const Config& config_;
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
  bool program_failed();
  ReadErrorResult read_result(std::uint64_t bytes, std::uint32_t retry);

 private:
  const Config& config_;
  std::mt19937_64 random_;
  std::uint64_t injected_program_failures_ = 0;
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

enum class EventType { DispatchWake, RefreshManagerWake,
                       HostArrival, HostCommandDone, SubreqReady,
                       NandSuspendDone, NandReadDone,
                       NandDataInDone, NandProgramDone, NandEraseDone,
                       NandRefreshDone, NandDataOutDone, SubreqDone };
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

class StatsCollector {
 public:
  void record_request(const Request& request);
  void record_subrequest(const SubRequest& subrequest);
  void record_array_issue(std::uint32_t stack, std::uint32_t die,
                          std::uint32_t plane, SimTime start, SimTime end);
  void record_fabric_transfer(std::uint32_t stack, std::uint32_t port,
                              SimTime start, SimTime end);
  void record_host_transfer(std::uint32_t stack, std::uint32_t channel,
                            HostLinkDirection direction, SimTime start,
                            SimTime end);
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
  void set_topology(std::uint32_t stacks, std::uint32_t dies_per_stack,
                    std::uint32_t planes_per_die,
                    std::uint32_t ports_per_stack,
                    std::uint32_t host_channels_per_stack);
  void write(const std::string& output_dir, SimTime makespan) const;
  std::uint64_t completed_requests() const { return completed_requests_; }
  double mean_latency_ns() const;
  double p99_latency_ns() const;
  double percentile_latency_ns(double quantile) const;
  std::uint64_t failed_requests() const { return failed_requests_; }
  std::uint64_t read_retries() const { return read_retries_; }
  std::uint64_t corrected_reads() const { return corrected_reads_; }
  std::uint64_t uncorrectable_reads() const { return uncorrectable_reads_; }
  std::uint64_t program_failures() const { return program_failures_; }
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

 private:
  struct Interval { SimTime start = 0; SimTime end = 0; };
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
  std::uint64_t read_retries_ = 0;
  std::uint64_t corrected_reads_ = 0;
  std::uint64_t uncorrectable_reads_ = 0;
  std::uint64_t program_failures_ = 0;
  std::uint64_t program_failure_notices_ = 0;
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
  std::unordered_map<std::uint32_t, SimTime> plane_busy_ns_;
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
  std::vector<std::vector<Interval>> stack_array_intervals_;
  std::vector<std::vector<Interval>> stack_fabric_intervals_;
  std::vector<std::vector<Interval>> stack_host_intervals_;
  std::vector<std::vector<Interval>> die_intervals_;
  std::vector<std::vector<Interval>> port_intervals_;
  std::vector<std::vector<Interval>> host_channel_intervals_;
  std::vector<QueueDepthSample> queue_depth_samples_;
};

class Simulator {
 public:
  explicit Simulator(Config config);
  void submit(const TraceEntry& entry);
  void run();
  void run(IRequestSource& source);
  void run_until(SimTime until);
  const StatsCollector& stats() const { return stats_; }
  SimTime now() const { return now_; }
  SimulationPhase phase() const { return phase_; }
  PageState page_state(const PhysicalAddr& paddr) const;
  BlockState block_state(const PhysicalAddr& paddr) const;
  SimTime block_ready_at(const PhysicalAddr& paddr) const;
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
  std::uint32_t plane_index(const PhysicalAddr& paddr) const;
  Plane& plane(const PhysicalAddr& paddr);
  const Plane& plane(const PhysicalAddr& paddr) const;
  DieState& die(const PhysicalAddr& paddr);
  const DieState& die(const PhysicalAddr& paddr) const;
  SimTime command_ready_time(const SubRequest& subrequest) const;
  void claim_command(const SubRequest& subrequest, SimTime now,
                     bool shared_command);
  std::uint64_t page_key(const PhysicalAddr& paddr) const;
  std::uint64_t block_key(const PhysicalAddr& paddr) const;
  void set_transient_page_state(const PhysicalAddr& paddr, PageState state);
  void clear_transient_page_state(const PhysicalAddr& paddr);
  void materialize_initialized_page(const PhysicalAddr& paddr);
  bool is_measured(std::uint64_t request_id) const;
  void record_queue_depth();
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
  AddressMapper mapper_;
  HostRouter host_router_;
  ReliabilityModel reliability_;
  HostGcManager host_gc_manager_;
  RefreshManager refresh_manager_;
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
  std::unordered_map<std::uint64_t, CopyJob> copy_jobs_;
  std::vector<PendingRecovery> pending_recoveries_;
  std::uint64_t next_copy_job_id_ = 0;
  std::array<std::uint64_t, 4> queue_depth_{};
  SimulationPhase phase_ = SimulationPhase::Initialize;
  bool streaming_submission_ = false;
  StatsCollector stats_;
};

}  // namespace hbfsim
