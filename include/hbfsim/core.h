#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace hbfsim {

using SimTime = std::uint64_t;

enum class OpType { Read, Write, Erase, Refresh };
enum class MappingPolicy { Linear, FineStripe, BurstStripe, HostManaged };
enum class TransactionSource { User, Mapping, Maintenance, GarbageCollection, Recovery };
enum class BlockState { Free, Open, Closed, Erasing, Bad };
enum class PageState { Erased, Reading, Programming, Valid, Invalid, Failed };
enum class ReadErrorStatus { Clean, Corrected, Uncorrectable };

std::string to_string(OpType op);
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
  MappingPolicy mapping_policy = MappingPolicy::BurstStripe;
  std::uint64_t burst_size = 2 * 1024 * 1024;
  SimTime write_starvation_ns = 100'000;
  std::uint32_t max_consecutive_reads = 64;
  bool strict_media_validation = false;
  bool suspend_resume_enabled = false;
  bool multi_plane_enabled = false;
  std::uint32_t max_multi_plane_width = 2;
  bool cache_program_enabled = false;
  double program_failure_rate = 0.0;
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
};

struct Request {
  std::uint64_t id = 0;
  SimTime arrival_time = 0;
  OpType op = OpType::Read;
  std::uint64_t logical_addr = 0;
  std::uint64_t size = 0;
  std::uint32_t stream_id = 0;
  std::uint32_t pending_subreqs = 0;
  SimTime complete_time = 0;
  bool failed = false;
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
  SimTime arrival_time = 0;
  SimTime enqueue_time = 0;
  SimTime issue_time = 0;
  SimTime complete_time = 0;
  SimTime ready_time = 0;
  SimTime array_active_since = 0;
  SimTime array_completion_time = 0;
  SimTime suspended_remaining_ns = 0;
  std::uint32_t read_attempts = 0;
  bool suspended = false;
  bool failed = false;
};

struct TraceEntry {
  SimTime timestamp_ns;
  OpType op;
  std::uint64_t address;
  std::uint64_t size;
  std::uint32_t stream;
};

class TraceReader {
 public:
  static std::vector<TraceEntry> read_csv(const std::string& path);
};

class AddressMapper {
 public:
  explicit AddressMapper(const Config& config);
  PhysicalAddr placement(std::uint64_t lpn) const;
  PhysicalAddr preview_write(std::uint64_t lpn) const;
  PhysicalAddr map_read(std::uint64_t lpn) const;
  PhysicalAddr prepare_write(std::uint64_t lpn);
  void commit_write(std::uint64_t lpn, const PhysicalAddr& paddr);
  std::optional<PhysicalAddr> lookup(std::uint64_t lpn) const;
  void on_erase(const PhysicalAddr& block_addr);
  std::uint32_t flat_plane(const PhysicalAddr& addr) const;

 private:
  PhysicalAddr base_map(std::uint64_t lpn) const;
  PhysicalAddr allocate_host_managed(std::uint64_t lpn);
  const Config& config_;
  std::unordered_map<std::uint64_t, PhysicalAddr> l2p_;
  std::vector<std::uint64_t> frontiers_;
};

class LinkResource {
 public:
  LinkResource() = default;
  LinkResource(double bytes_per_ns, SimTime fixed_latency_ns)
      : bytes_per_ns_(bytes_per_ns), fixed_latency_ns_(fixed_latency_ns) {}
  SimTime reserve(SimTime now, std::uint64_t bytes);
  SimTime free_at() const { return free_at_; }

 private:
  SimTime free_at_ = 0;
  double bytes_per_ns_ = 1.0;
  SimTime fixed_latency_ns_ = 0;
};

class DataFabric {
 public:
  DataFabric() = default;
  DataFabric(std::uint32_t ports, double aggregate_bytes_per_ns, SimTime fixed_latency_ns);
  SimTime reserve(SimTime now, std::uint64_t bytes, std::uint32_t port);

 private:
  std::vector<SimTime> port_free_at_;
  SimTime aggregate_free_at_ = 0;
  double aggregate_bytes_per_ns_ = 1.0;
  double port_bytes_per_ns_ = 1.0;
  SimTime fixed_latency_ns_ = 0;
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
  bool busy = false;
  std::deque<std::uint64_t> reads;
  std::deque<std::uint64_t> writes;
  std::deque<std::uint64_t> erases;
  std::deque<std::uint64_t> refreshes;
  std::uint32_t consecutive_reads = 0;
  SimTime ready_at = 0;
  bool data_register_busy = false;
  bool suspend_pending = false;
  std::optional<std::uint64_t> active_subrequest;
  std::optional<std::uint64_t> suspended_subrequest;
  std::optional<std::uint64_t> cached_write;
  std::vector<BlockMeta> blocks;
};

enum class EventType { DispatchWake, HostArrival, HostCommandDone, SubreqReady,
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

class StatsCollector {
 public:
  void record_request(const Request& request);
  void record_subrequest(const SubRequest& subrequest);
  void record_plane_issue(std::uint32_t plane, SimTime start, SimTime end);
  void record_read_retry() { ++read_retries_; }
  void record_corrected_read() { ++corrected_reads_; }
  void record_uncorrectable_read() { ++uncorrectable_reads_; }
  void record_program_failure() { ++program_failures_; }
  void set_total_planes(std::uint32_t total_planes) { total_planes_ = total_planes; }
  void write(const std::string& output_dir, SimTime makespan) const;
  std::uint64_t completed_requests() const { return completed_requests_; }
  double mean_latency_ns() const;
  double p99_latency_ns() const;
  std::uint64_t failed_requests() const { return failed_requests_; }
  std::uint64_t read_retries() const { return read_retries_; }
  std::uint64_t corrected_reads() const { return corrected_reads_; }
  std::uint64_t uncorrectable_reads() const { return uncorrectable_reads_; }
  std::uint64_t program_failures() const { return program_failures_; }

 private:
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
  std::uint32_t total_planes_ = 0;
  std::optional<SimTime> first_arrival_;
  SimTime last_completion_ = 0;
  std::vector<SimTime> latencies_;
  std::vector<SimTime> subrequest_waits_;
  std::map<OpType, std::vector<SimTime>> op_latencies_;
  std::map<OpType, std::uint64_t> op_bytes_;
  std::unordered_map<std::uint32_t, SimTime> plane_busy_ns_;
};

class Simulator {
 public:
  explicit Simulator(Config config);
  void submit(const TraceEntry& entry);
  void run();
  void run_until(SimTime until);
  const StatsCollector& stats() const { return stats_; }
  SimTime now() const { return now_; }
  PageState page_state(const PhysicalAddr& paddr) const;
  BlockState block_state(const PhysicalAddr& paddr) const;
  SimTime block_ready_at(const PhysicalAddr& paddr) const;
  SimTime die_ready_at(const PhysicalAddr& paddr) const;

 private:
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
  SimTime reserve_host(const PhysicalAddr& paddr, SimTime now, std::uint64_t bytes);
  SimTime reserve_fabric(const PhysicalAddr& paddr, SimTime now, std::uint64_t bytes);
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
  void set_transient_page_state(const PhysicalAddr& paddr, PageState state);
  void clear_transient_page_state(const PhysicalAddr& paddr);

  Config config_;
  AddressMapper mapper_;
  ReliabilityModel reliability_;
  SimTime now_ = 0;
  std::uint64_t next_event_seq_ = 0;
  std::uint64_t next_request_id_ = 0;
  std::uint64_t next_subrequest_id_ = 0;
  std::uint64_t submitted_requests_ = 0;
  std::priority_queue<Event, std::vector<Event>, EventCompare> events_;
  std::unordered_map<std::uint64_t, Request> requests_;
  std::unordered_map<std::uint64_t, SubRequest> subrequests_;
  std::vector<Plane> planes_;
  std::vector<DieState> dies_;
  std::vector<std::uint32_t> active_per_die_;
  std::vector<std::uint32_t> active_per_stack_;
  std::vector<std::uint32_t> dispatch_cursor_per_stack_;
  std::vector<SimTime> dispatch_wake_at_;
  std::vector<std::deque<std::uint64_t>> program_ready_;
  std::vector<LinkResource> host_links_;
  std::vector<DataFabric> fabrics_;
  std::unordered_map<std::uint64_t, PageState> transient_page_states_;
  StatsCollector stats_;
};

}  // namespace hbfsim
