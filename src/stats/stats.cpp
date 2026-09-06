#include "hbfsim/stats/stats.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hbfsim {
namespace {

std::size_t histogram_bucket(SimTime value) {
  constexpr std::size_t linear_buckets = 1024;
  constexpr std::size_t sub_buckets = 64;
  if (value < linear_buckets) return static_cast<std::size_t>(value);
  const auto exponent = static_cast<std::size_t>(std::bit_width(value) - 1);
  const auto base = SimTime{1} << exponent;
  const auto fraction = static_cast<long double>(value - base) /
                        static_cast<long double>(base);
  const auto sub = std::min<std::size_t>(
      sub_buckets - 1, static_cast<std::size_t>(fraction * sub_buckets));
  return linear_buckets + (exponent - 10) * sub_buckets + sub;
}

double histogram_bucket_upper(std::size_t bucket) {
  constexpr std::size_t linear_buckets = 1024;
  constexpr std::size_t sub_buckets = 64;
  if (bucket < linear_buckets) return static_cast<double>(bucket);
  const auto relative = bucket - linear_buckets;
  const auto exponent = relative / sub_buckets + 10;
  const auto sub = relative % sub_buckets;
  const long double base = std::ldexp(1.0L, static_cast<int>(exponent));
  return static_cast<double>(
      base + std::ceil(base * static_cast<long double>(sub + 1) /
                       static_cast<long double>(sub_buckets)) -
      1.0L);
}

void add_breakdown(LatencyBreakdown& total,
                   const LatencyBreakdown& value) {
  total.host_command_wait_ns += value.host_command_wait_ns;
  total.host_command_service_ns += value.host_command_service_ns;
  total.host_data_wait_ns += value.host_data_wait_ns;
  total.host_data_service_ns += value.host_data_service_ns;
  total.nand_queue_wait_ns += value.nand_queue_wait_ns;
  total.nand_command_wait_ns += value.nand_command_wait_ns;
  total.array_service_ns += value.array_service_ns;
  total.auto_erase_service_ns += value.auto_erase_service_ns;
  total.fabric_wait_ns += value.fabric_wait_ns;
  total.fabric_service_ns += value.fabric_service_ns;
}

}  // namespace

void LatencyHistogram::record(SimTime value) {
  ++buckets_.at(histogram_bucket(value));
  ++count_;
  sum_ += value;
}

double LatencyHistogram::mean() const {
  return count_ == 0 ? 0.0 : static_cast<double>(sum_ / count_);
}

double LatencyHistogram::percentile(double quantile) const {
  if (count_ == 0) return 0.0;
  if (!(quantile > 0.0 && quantile <= 1.0))
    throw std::invalid_argument("percentile must be in (0,1]");
  const auto target = static_cast<std::uint64_t>(
      std::ceil(static_cast<long double>(count_) * quantile));
  std::uint64_t cumulative = 0;
  for (std::size_t i = 0; i < buckets_.size(); ++i) {
    cumulative += buckets_[i];
    if (cumulative >= target) return histogram_bucket_upper(i);
  }
  return histogram_bucket_upper(buckets_.size() - 1);
}

void StatsCollector::set_topology(std::uint32_t stacks,
                                  std::uint32_t dies_per_stack,
                                  std::uint32_t planes_per_die,
                                  std::uint32_t ports_per_stack,
                                  std::uint32_t host_channels_per_stack) {
  stacks_ = stacks;
  dies_per_stack_ = dies_per_stack;
  planes_per_die_ = planes_per_die;
  ports_per_stack_ = ports_per_stack;
  host_channels_per_stack_ = host_channels_per_stack;
  resources_.configure(stacks, dies_per_stack, planes_per_die,
                       ports_per_stack, host_channels_per_stack);
}

void StatsCollector::record_request(const Request& request) {
  ++completed_requests_;
  if (request.failed) ++failed_requests_;
  const auto completion = hbf_completion_class(request.status);
  if (completion == HbfCompletionClass::SuccessWithAdvisory)
    ++advisory_requests_;
  else if (completion == HbfCompletionClass::RetryRequired)
    ++retry_required_requests_;
  if (request.op == OpType::Read) ++read_requests_;
  if (request.op == OpType::Write) ++write_requests_;
  if (request.op == OpType::Read || request.op == OpType::Write) {
    completed_bytes_ += request.size;
    if (!request.failed) successful_bytes_ += request.size;
  }
  op_bytes_[request.op] += request.size;
  if (!first_arrival_ || request.arrival_time < *first_arrival_)
    first_arrival_ = request.arrival_time;
  last_completion_ = std::max(last_completion_, request.complete_time);
  const auto latency = request.complete_time - request.arrival_time;
  latencies_.record(latency);
  op_latencies_[request.op].record(latency);
}

void StatsCollector::record_dlu_completed(const HbfDlu::Timing& timing) {
  ++completed_dlus_;
  dlu_assembly_latencies_.record(timing.assembly_latency_ns());
  dlu_total_h2d_wait_ns_ += timing.total_h2d_wait_ns;
  dlu_total_h2d_service_ns_ += timing.total_h2d_service_ns;
}

void StatsCollector::record_dlu_rejection(HbfStatus status) {
  if (status == HbfStatus::OverlappingAddress)
    ++dlu_overlaps_;
  else if (status == HbfStatus::MaxPendingDluReached)
    ++dlu_capacity_rejections_;
}

void StatsCollector::record_subrequest(const SubRequest& subrequest) {
  add_breakdown(latency_breakdown_[subrequest.op], subrequest.latency);
  ++latency_breakdown_samples_[subrequest.op];
  const auto key = std::make_pair(subrequest.source, subrequest.op);
  add_breakdown(source_latency_breakdown_[key], subrequest.latency);
  ++source_latency_samples_[key];
  source_bytes_[key] += subrequest.bytes;
  if (subrequest.failed) ++source_failures_[key];
}

void StatsCollector::record_remap_commit(TransactionSource source,
                                         SimTime latency_ns) {
  ++remap_commits_;
  if (source == TransactionSource::Recovery)
    recovery_latencies_.record(latency_ns);
  else if (source == TransactionSource::GarbageCollection)
    gc_latencies_.record(latency_ns);
  else if (source == TransactionSource::Refresh)
    refresh_latencies_.record(latency_ns);
}

void StatsCollector::record_copy_job(TransactionSource source, bool failed) {
  if (source == TransactionSource::Recovery) {
    if (failed)
      ++failed_recovery_jobs_;
    else
      ++completed_recovery_jobs_;
  } else if (source == TransactionSource::GarbageCollection) {
    if (failed)
      ++failed_gc_jobs_;
    else
      ++completed_gc_jobs_;
  } else if (source == TransactionSource::Refresh) {
    if (failed)
      ++failed_refresh_jobs_;
    else
      ++completed_refresh_jobs_;
  }
}

void StatsCollector::record_copy_buffer_high_watermark(
    TransactionSource source, std::uint64_t bytes) {
  auto& high_watermark = copy_buffer_high_watermarks_[source];
  high_watermark = std::max(high_watermark, bytes);
}

std::uint64_t StatsCollector::copy_buffer_high_watermark(
    TransactionSource source) const {
  const auto it = copy_buffer_high_watermarks_.find(source);
  return it == copy_buffer_high_watermarks_.end() ? 0 : it->second;
}

void StatsCollector::observe_free_stripes(
    std::uint64_t free_stripes, std::uint64_t host_visible_stripes) {
  host_visible_stripes_ = host_visible_stripes;
  if (!observed_free_stripes_) {
    min_free_stripes_ = free_stripes;
    observed_free_stripes_ = true;
  } else {
    min_free_stripes_ = std::min(min_free_stripes_, free_stripes);
  }
}

std::uint64_t StatsCollector::source_bytes(TransactionSource source,
                                           OpType op) const {
  const auto it = source_bytes_.find(std::make_pair(source, op));
  return it == source_bytes_.end() ? 0 : it->second;
}

std::uint64_t StatsCollector::host_rewrite_jobs(TransactionSource source,
                                                bool failed) const {
  const auto& values = failed ? failed_host_rewrite_jobs_
                              : completed_host_rewrite_jobs_;
  const auto it = values.find(source);
  return it == values.end() ? 0 : it->second;
}

double StatsCollector::host_rewrite_mean_latency_ns(
    TransactionSource source) const {
  const auto it = host_rewrite_latencies_.find(source);
  return it == host_rewrite_latencies_.end() ? 0.0 : it->second.mean();
}

double StatsCollector::host_rewrite_p95_latency_ns(
    TransactionSource source) const {
  const auto it = host_rewrite_latencies_.find(source);
  return it == host_rewrite_latencies_.end() ? 0.0 : it->second.percentile(0.95);
}

void StatsCollector::record_queue_depth(SimTime time, std::uint64_t reads,
                                        std::uint64_t writes,
                                        std::uint64_t erases,
                                        std::uint64_t refreshes,
                                        std::uint64_t active_planes) {
  QueueDepthSample sample{time, reads, writes, erases, refreshes,
                          active_planes};
  latest_queue_depth_ = sample;
  if (!queue_depth_samples_.empty() &&
      queue_depth_samples_.back().time == time)
    queue_depth_samples_.back() = sample;
  else if (queue_depth_samples_.empty() ||
           queue_depth_sample_interval_ns_ == 0 ||
           time - queue_depth_samples_.back().time >=
               queue_depth_sample_interval_ns_)
    queue_depth_samples_.push_back(sample);
}

double StatsCollector::mean_latency_ns() const { return latencies_.mean(); }

double StatsCollector::p99_latency_ns() const {
  return percentile_latency_ns(0.99);
}

double StatsCollector::percentile_latency_ns(double quantile) const {
  return latencies_.percentile(quantile);
}

void StatsCollector::write(const std::string& output_dir,
                           SimTime makespan) const {
  std::filesystem::create_directories(output_dir);
  std::ofstream summary(std::filesystem::path(output_dir) / "summary.csv");
  if (!summary) throw std::runtime_error("cannot write results");
  const SimTime measurement_ns = first_arrival_
      ? std::max<SimTime>(1, last_completion_ - *first_arrival_)
      : std::max<SimTime>(1, makespan);
  const double seconds = static_cast<double>(measurement_ns) / 1e9;
  const SimTime resource_measurement_ns = first_arrival_
      ? std::max<SimTime>(1, makespan - *first_arrival_)
      : std::max<SimTime>(1, makespan);
  summary << "metric,value\ncompleted_requests," << completed_requests_
          << "\nread_requests," << read_requests_
          << "\nwrite_requests," << write_requests_
          << "\ncompleted_bytes," << completed_bytes_
          << "\nsuccessful_bytes," << successful_bytes_
          << "\nfailed_requests," << failed_requests_
          << "\nadvisory_requests," << advisory_requests_
          << "\nretry_required_requests," << retry_required_requests_
          << "\ncompleted_dlus," << completed_dlus_
          << "\ndlu_timeouts," << dlu_timeouts_
          << "\ndlu_overlaps," << dlu_overlaps_
          << "\ndlu_capacity_rejections," << dlu_capacity_rejections_
          << "\ndlu_forwarded_reads," << dlu_forwarded_reads_
          << "\ndlu_forwarded_bytes," << dlu_forwarded_bytes_
          << "\ndlu_pending_reads," << dlu_pending_reads_
          << "\ndlu_assembly_mean_ns," << dlu_assembly_latencies_.mean()
          << "\ndlu_assembly_p95_ns,"
          << dlu_assembly_latencies_.percentile(0.95)
          << "\ndlu_assembly_p99_ns,"
          << dlu_assembly_latencies_.percentile(0.99)
          << "\ndlu_total_h2d_wait_ns," << dlu_total_h2d_wait_ns_
          << "\ndlu_total_h2d_service_ns," << dlu_total_h2d_service_ns_
          << "\nread_cache_hits," << read_cache_hits_
          << "\nread_cache_misses," << read_cache_misses_
          << "\nread_cache_evictions," << read_cache_evictions_
          << "\nread_cache_hit_bytes," << read_cache_hit_bytes_
          << "\nread_cache_hit_ratio,"
          << (read_cache_hits_ + read_cache_misses_ == 0
                  ? 0.0
                  : static_cast<double>(read_cache_hits_) /
                        (read_cache_hits_ + read_cache_misses_))
          << "\nbatch_read_emissions," << batch_read_emissions_
          << "\nbatch_read_pages," << batch_read_pages_
          << "\nbatch_read_aggregation_delay_ns,"
          << batch_read_aggregation_delay_ns_
          << "\nprogram_failures," << program_failures_
          << "\nprogram_failure_notices," << program_failure_notices_
          << "\nerase_failures," << erase_failures_
          << "\npage0_auto_erases," << page0_auto_erases_
          << "\nretired_blocks," << retired_blocks_
          << "\nretired_stripes," << retired_stripes_
          << "\ntotal_physical_capacity_bytes,"
          << total_physical_capacity_bytes_
          << "\nusable_physical_capacity_bytes,"
          << usable_physical_capacity_bytes_
          << "\nhost_visible_capacity_bytes,"
          << host_visible_capacity_bytes_
          << "\nparallelism_groups," << parallelism_groups_
          << "\nstripe_width_pages," << stripe_width_pages_
          << "\nstripe_capacity_pages," << stripe_capacity_pages_
          << "\nremap_commits," << remap_commits_
          << "\naborted_migrations," << aborted_migrations_
          << "\ncompleted_recovery_jobs," << completed_recovery_jobs_
          << "\nfailed_recovery_jobs," << failed_recovery_jobs_
          << "\ncompleted_host_gc_jobs," << completed_gc_jobs_
          << "\nfailed_host_gc_jobs," << failed_gc_jobs_
          << "\nhost_gc_cycles_started," << host_gc_cycles_started_
          << "\nhost_gc_high_watermark_reached,"
          << host_gc_high_watermark_reached_
          << "\nhost_gc_stalls," << host_gc_stalls_
          << "\nautomatic_host_gc_jobs," << automatic_gc_jobs_
          << "\nautomatic_host_gc_erase_only_jobs,"
          << automatic_gc_erase_only_jobs_
          << "\nautomatic_refresh_jobs," << automatic_refresh_jobs_
          << "\ncompleted_refresh_jobs," << completed_refresh_jobs_
          << "\nfailed_refresh_jobs," << failed_refresh_jobs_
          << "\nrefresh_deadline_misses," << refresh_deadline_misses_
          << "\nrefresh_deferred_no_space,"
          << refresh_deferred_no_space_
          << "\ncompleted_host_replay_jobs,"
          << host_rewrite_jobs(TransactionSource::HostReplay, false)
          << "\nfailed_host_replay_jobs,"
          << host_rewrite_jobs(TransactionSource::HostReplay, true)
          << "\ncompleted_host_refresh_jobs,"
          << host_rewrite_jobs(TransactionSource::HostRefresh, false)
          << "\ncompleted_host_wear_level_jobs,"
          << host_rewrite_jobs(TransactionSource::HostWearLevel, false)
          << "\nhost_replay_mean_latency_ns,"
          << host_rewrite_mean_latency_ns(TransactionSource::HostReplay)
          << "\nhost_refresh_mean_latency_ns,"
          << host_rewrite_mean_latency_ns(TransactionSource::HostRefresh)
          << "\nhost_wear_level_mean_latency_ns,"
          << host_rewrite_mean_latency_ns(TransactionSource::HostWearLevel)
          << "\nhost_visible_stripes," << host_visible_stripes_
          << "\nmin_free_stripes," << min_free_stripes_
          << "\nrecovery_read_bytes,"
          << source_bytes(TransactionSource::Recovery, OpType::Read)
          << "\nrecovery_program_bytes,"
          << source_bytes(TransactionSource::Recovery, OpType::Write)
          << "\nhost_gc_read_bytes,"
          << source_bytes(TransactionSource::GarbageCollection, OpType::Read)
          << "\nhost_gc_program_bytes,"
          << source_bytes(TransactionSource::GarbageCollection, OpType::Write)
          << "\nrefresh_read_bytes,"
          << source_bytes(TransactionSource::Refresh, OpType::Read)
          << "\nrefresh_program_bytes,"
          << source_bytes(TransactionSource::Refresh, OpType::Write)
          << "\nrecovery_mean_latency_ns," << recovery_latencies_.mean()
          << "\nrecovery_p95_latency_ns,"
          << recovery_latencies_.percentile(0.95)
          << "\nrecovery_p99_latency_ns,"
          << recovery_latencies_.percentile(0.99)
          << "\nhost_gc_mean_latency_ns," << gc_latencies_.mean()
          << "\nrefresh_mean_latency_ns," << refresh_latencies_.mean()
          << "\nrefresh_p95_latency_ns,"
          << refresh_latencies_.percentile(0.95)
          << "\nrefresh_p99_latency_ns,"
          << refresh_latencies_.percentile(0.99)
          << "\nrecovery_copy_buffer_high_watermark_bytes,"
          << copy_buffer_high_watermark(TransactionSource::Recovery)
          << "\nhost_gc_copy_buffer_high_watermark_bytes,"
          << copy_buffer_high_watermark(
                 TransactionSource::GarbageCollection)
          << "\nrefresh_copy_buffer_high_watermark_bytes,"
          << copy_buffer_high_watermark(TransactionSource::Refresh)
          << "\nstripe_write_amplification,"
          << (source_bytes(TransactionSource::User, OpType::Write) == 0
                  ? 0.0
                  : static_cast<double>(
                        source_bytes(TransactionSource::User, OpType::Write) +
                        source_bytes(TransactionSource::Recovery,
                                     OpType::Write) +
                        source_bytes(TransactionSource::GarbageCollection,
                                     OpType::Write) +
                        source_bytes(TransactionSource::Refresh,
                                     OpType::Write)) /
                        source_bytes(TransactionSource::User, OpType::Write))
          << "\ncorrected_reads," << corrected_reads_
          << "\nuncorrectable_reads," << uncorrectable_reads_
          << "\nread_retries," << read_retries_
          << "\nmakespan_ns," << makespan
          << "\nmeasurement_duration_ns," << measurement_ns
          << "\nresource_measurement_duration_ns,"
          << resource_measurement_ns
          << "\nmean_latency_ns," << mean_latency_ns()
          << "\np50_latency_ns," << percentile_latency_ns(0.50)
          << "\np95_latency_ns," << percentile_latency_ns(0.95)
          << "\np99_latency_ns," << percentile_latency_ns(0.99)
          << "\np99_9_latency_ns," << percentile_latency_ns(0.999)
          << "\neffective_bandwidth_GBps,"
          << (static_cast<double>(completed_bytes_) / seconds / 1e9)
          << "\neffective_goodput_GBps,"
          << (static_cast<double>(successful_bytes_) / seconds / 1e9)
          << '\n';
  for (const auto& [op, histogram] : op_latencies_) {
    if (histogram.count() == 0) continue;
    summary << "op_" << to_string(op) << "_count," << histogram.count()
            << "\nop_" << to_string(op) << "_bytes," << op_bytes_.at(op)
            << "\nop_" << to_string(op) << "_bandwidth_GBps,"
            << (static_cast<double>(op_bytes_.at(op)) / seconds / 1e9)
            << "\nop_" << to_string(op) << "_mean_latency_ns,"
            << histogram.mean()
            << "\nop_" << to_string(op) << "_p50_latency_ns,"
            << histogram.percentile(0.50)
            << "\nop_" << to_string(op) << "_p95_latency_ns,"
            << histogram.percentile(0.95)
            << "\nop_" << to_string(op) << "_p99_latency_ns,"
            << histogram.percentile(0.99)
            << "\nop_" << to_string(op) << "_p99_9_latency_ns,"
            << histogram.percentile(0.999) << '\n';
  }

  std::ofstream dlu(std::filesystem::path(output_dir) / "dlu_summary.csv");
  dlu << "completed,timeouts,overlaps,capacity_rejections,forwarded_reads,"
         "forwarded_bytes,pending_reads,assembly_mean_ns,assembly_p95_ns,"
         "assembly_p99_ns,total_h2d_wait_ns,total_h2d_service_ns\n"
      << completed_dlus_ << ',' << dlu_timeouts_ << ',' << dlu_overlaps_
      << ',' << dlu_capacity_rejections_ << ',' << dlu_forwarded_reads_
      << ',' << dlu_forwarded_bytes_ << ',' << dlu_pending_reads_ << ','
      << dlu_assembly_latencies_.mean() << ','
      << dlu_assembly_latencies_.percentile(0.95) << ','
      << dlu_assembly_latencies_.percentile(0.99) << ','
      << dlu_total_h2d_wait_ns_ << ',' << dlu_total_h2d_service_ns_ << '\n';

  const auto total_planes = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(stacks_) * dies_per_stack_ *
      planes_per_die_);
  std::ofstream planes(std::filesystem::path(output_dir) /
                       "plane_utilization.csv");
  planes << "plane,busy_ns,utilization\n";
  for (std::uint32_t plane = 0; plane < total_planes; ++plane) {
    const auto busy = resources_.plane_busy(plane, makespan);
    planes << plane << ',' << busy << ','
           << static_cast<double>(busy) / resource_measurement_ns << '\n';
  }

  std::ofstream breakdown(std::filesystem::path(output_dir) /
                          "latency_breakdown.csv");
  breakdown << "op,samples,host_command_wait_ns,host_command_service_ns,"
               "host_data_wait_ns,host_data_service_ns,nand_queue_wait_ns,"
               "nand_command_wait_ns,array_service_ns,fabric_wait_ns,"
               "auto_erase_service_ns,fabric_service_ns\n";
  for (const auto& [op, total] : latency_breakdown_) {
    const auto samples = latency_breakdown_samples_.at(op);
    const auto average = [samples](SimTime value) {
      return samples ? static_cast<double>(value) / samples : 0.0;
    };
    breakdown << to_string(op) << ',' << samples << ','
              << average(total.host_command_wait_ns) << ','
              << average(total.host_command_service_ns) << ','
              << average(total.host_data_wait_ns) << ','
              << average(total.host_data_service_ns) << ','
              << average(total.nand_queue_wait_ns) << ','
              << average(total.nand_command_wait_ns) << ','
              << average(total.array_service_ns) << ','
              << average(total.fabric_wait_ns) << ','
              << average(total.auto_erase_service_ns) << ','
              << average(total.fabric_service_ns) << '\n';
  }

  std::ofstream source_breakdown(
      std::filesystem::path(output_dir) / "source_latency_breakdown.csv");
  source_breakdown
      << "source,op,samples,bytes,failed,host_command_wait_ns,"
         "host_command_service_ns,host_data_wait_ns,host_data_service_ns,"
         "nand_queue_wait_ns,nand_command_wait_ns,array_service_ns,"
         "fabric_wait_ns,auto_erase_service_ns,fabric_service_ns\n";
  for (const auto& [key, total] : source_latency_breakdown_) {
    const auto samples = source_latency_samples_.at(key);
    const auto failure = source_failures_.find(key);
    const auto failures = failure == source_failures_.end()
                              ? 0
                              : failure->second;
    const auto average = [samples](SimTime value) {
      return samples ? static_cast<double>(value) / samples : 0.0;
    };
    source_breakdown << to_string(key.first) << ',' << to_string(key.second)
                     << ',' << samples << ',' << source_bytes_.at(key) << ','
                     << failures << ','
                     << average(total.host_command_wait_ns) << ','
                     << average(total.host_command_service_ns) << ','
                     << average(total.host_data_wait_ns) << ','
                     << average(total.host_data_service_ns) << ','
                     << average(total.nand_queue_wait_ns) << ','
                     << average(total.nand_command_wait_ns) << ','
                     << average(total.array_service_ns) << ','
                     << average(total.fabric_wait_ns) << ','
                     << average(total.auto_erase_service_ns) << ','
                     << average(total.fabric_service_ns) << '\n';
  }

  const SimTime window_end = makespan;

  std::ofstream resources(std::filesystem::path(output_dir) /
                          "resource_utilization.csv");
  resources << "stack,array_only_ns,fabric_only_ns,overlap_ns,idle_ns,"
               "array_utilization,fabric_utilization,host_utilization,"
               "avg_active_planes,max_active_planes\n";
  for (std::uint32_t stack = 0; stack < stacks_; ++stack) {
    const auto array_ns = resources_.array_busy(stack, window_end);
    const auto fabric_ns = resources_.fabric_busy(stack, window_end);
    const auto host_ns = resources_.host_busy(stack, window_end);
    const auto overlap_ns =
        resources_.array_fabric_overlap(stack, window_end);
    const auto union_ns = array_ns + fabric_ns - overlap_ns;
    resources << stack << ',' << array_ns - overlap_ns << ','
              << fabric_ns - overlap_ns << ',' << overlap_ns << ','
              << (resource_measurement_ns > union_ns
                      ? resource_measurement_ns - union_ns
                      : 0)
              << ','
              << static_cast<double>(array_ns) / resource_measurement_ns << ','
              << static_cast<double>(fabric_ns) / resource_measurement_ns
              << ',' << static_cast<double>(host_ns) /
                            resource_measurement_ns
              << ','
              << static_cast<double>(
                     resources_.active_plane_area(stack, window_end) /
                     resource_measurement_ns)
              << ',' << resources_.max_active_planes(stack) << '\n';
  }

  std::ofstream ports(std::filesystem::path(output_dir) /
                      "data_port_utilization.csv");
  ports << "stack,port,busy_ns,utilization\n";
  for (std::uint32_t stack = 0; stack < stacks_; ++stack) {
    for (std::uint32_t port = 0; port < ports_per_stack_; ++port) {
      const auto busy = resources_.port_busy(
          static_cast<std::uint32_t>(
              static_cast<std::size_t>(stack) * ports_per_stack_ + port),
          window_end);
      ports << stack << ',' << port << ',' << busy << ','
            << static_cast<double>(busy) / resource_measurement_ns << '\n';
    }
  }

  std::ofstream dies(std::filesystem::path(output_dir) /
                     "die_utilization.csv");
  dies << "stack,die,busy_ns,utilization\n";
  for (std::uint32_t stack = 0; stack < stacks_; ++stack) {
    for (std::uint32_t die = 0; die < dies_per_stack_; ++die) {
      const auto busy = resources_.die_busy(
          static_cast<std::uint32_t>(
              static_cast<std::size_t>(stack) * dies_per_stack_ + die),
          window_end);
      dies << stack << ',' << die << ',' << busy << ','
           << static_cast<double>(busy) / resource_measurement_ns << '\n';
    }
  }

  std::ofstream hosts(std::filesystem::path(output_dir) /
                      "host_channel_utilization.csv");
  hosts << "stack,channel,busy_ns,utilization\n";
  for (std::uint32_t stack = 0; stack < stacks_; ++stack) {
    for (std::uint32_t channel = 0; channel < host_channels_per_stack_;
         ++channel) {
      const auto busy = resources_.host_channel_busy(
          static_cast<std::uint32_t>(
              static_cast<std::size_t>(stack) *
                  host_channels_per_stack_ +
              channel),
          window_end);
      hosts << stack << ',' << channel << ',' << busy << ','
            << static_cast<double>(busy) / resource_measurement_ns << '\n';
    }
  }

  std::ofstream queues(std::filesystem::path(output_dir) /
                       "queue_depth.csv");
  queues << "time_ns,user_read,user_write,erase,refresh,active_planes\n";
  for (const auto& sample : queue_depth_samples_)
    queues << sample.time << ',' << sample.reads << ',' << sample.writes
           << ',' << sample.erases << ',' << sample.refreshes << ','
           << sample.active_planes << '\n';
  if (latest_queue_depth_ &&
      (queue_depth_samples_.empty() ||
       queue_depth_samples_.back().time != latest_queue_depth_->time))
    queues << latest_queue_depth_->time << ',' << latest_queue_depth_->reads
           << ',' << latest_queue_depth_->writes << ','
           << latest_queue_depth_->erases << ','
           << latest_queue_depth_->refreshes << ','
           << latest_queue_depth_->active_planes << '\n';
}

}  // namespace hbfsim
