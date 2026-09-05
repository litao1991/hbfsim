#pragma once

#include "hbfsim/common/types.h"
#include "hbfsim/protocol/dlu.h"
#include "hbfsim/protocol/request.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hbfsim {

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
  void record_batch_read(std::uint64_t pages, SimTime aggregation_delay_ns) {
    ++batch_read_emissions_;
    batch_read_pages_ += pages;
    batch_read_aggregation_delay_ns_ += aggregation_delay_ns;
  }
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
  void record_host_replay_job(bool failed, SimTime latency_ns) {
    if (failed)
      ++failed_host_replay_jobs_;
    else
      ++completed_host_replay_jobs_;
    host_replay_latencies_.record(latency_ns);
  }
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
  std::uint64_t batch_read_emissions() const { return batch_read_emissions_; }
  std::uint64_t batch_read_pages() const { return batch_read_pages_; }
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
  std::uint64_t completed_host_replay_jobs() const {
    return completed_host_replay_jobs_;
  }
  std::uint64_t failed_host_replay_jobs() const {
    return failed_host_replay_jobs_;
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
  std::uint64_t completed_host_replay_jobs_ = 0;
  std::uint64_t failed_host_replay_jobs_ = 0;
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
  std::uint64_t batch_read_emissions_ = 0;
  std::uint64_t batch_read_pages_ = 0;
  SimTime batch_read_aggregation_delay_ns_ = 0;
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
  LatencyHistogram host_replay_latencies_;
  std::map<TransactionSource, std::uint64_t>
      copy_buffer_high_watermarks_;
  ResourceTracker resources_;
  SimTime queue_depth_sample_interval_ns_ = 1'000;
  std::vector<QueueDepthSample> queue_depth_samples_;
  std::optional<QueueDepthSample> latest_queue_depth_;
};

}  // namespace hbfsim
