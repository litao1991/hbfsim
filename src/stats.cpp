#include "hbfsim/core.h"

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
  stack_array_intervals_.resize(stacks);
  stack_fabric_intervals_.resize(stacks);
  stack_host_intervals_.resize(stacks);
  die_intervals_.resize(static_cast<std::size_t>(stacks) * dies_per_stack);
  port_intervals_.resize(static_cast<std::size_t>(stacks) * ports_per_stack);
  host_channel_intervals_.resize(
      static_cast<std::size_t>(stacks) * host_channels_per_stack);
}

void StatsCollector::record_request(const Request& request) {
  ++completed_requests_;
  if (request.failed) ++failed_requests_;
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

void StatsCollector::record_array_issue(std::uint32_t stack,
                                        std::uint32_t die,
                                        std::uint32_t plane,
                                        SimTime start, SimTime end) {
  if (end <= start) return;
  plane_busy_ns_[plane] += end - start;
  stack_array_intervals_.at(stack).push_back({start, end});
  const auto die_index = static_cast<std::size_t>(stack) * dies_per_stack_ + die;
  die_intervals_.at(die_index).push_back({start, end});
}

void StatsCollector::record_fabric_transfer(std::uint32_t stack,
                                            std::uint32_t port,
                                            SimTime start, SimTime end) {
  if (end <= start) return;
  stack_fabric_intervals_.at(stack).push_back({start, end});
  const auto index = static_cast<std::size_t>(stack) * ports_per_stack_ +
                     port % ports_per_stack_;
  port_intervals_.at(index).push_back({start, end});
}

void StatsCollector::record_host_transfer(std::uint32_t stack,
                                          std::uint32_t channel,
                                          HostLinkDirection,
                                          SimTime start, SimTime end) {
  if (end <= start) return;
  stack_host_intervals_.at(stack).push_back({start, end});
  const auto index = static_cast<std::size_t>(stack) *
                         host_channels_per_stack_ +
                     channel % host_channels_per_stack_;
  host_channel_intervals_.at(index).push_back({start, end});
}

void StatsCollector::record_queue_depth(SimTime time, std::uint64_t reads,
                                        std::uint64_t writes,
                                        std::uint64_t erases,
                                        std::uint64_t refreshes,
                                        std::uint64_t active_planes) {
  QueueDepthSample sample{time, reads, writes, erases, refreshes,
                          active_planes};
  if (!queue_depth_samples_.empty() &&
      queue_depth_samples_.back().time == time)
    queue_depth_samples_.back() = sample;
  else
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
  summary << "metric,value\ncompleted_requests," << completed_requests_
          << "\nread_requests," << read_requests_
          << "\nwrite_requests," << write_requests_
          << "\ncompleted_bytes," << completed_bytes_
          << "\nsuccessful_bytes," << successful_bytes_
          << "\nfailed_requests," << failed_requests_
          << "\nprogram_failures," << program_failures_
          << "\nprogram_failure_notices," << program_failure_notices_
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

  const auto total_planes = static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(stacks_) * dies_per_stack_ *
      planes_per_die_);
  std::ofstream planes(std::filesystem::path(output_dir) /
                       "plane_utilization.csv");
  planes << "plane,busy_ns,utilization\n";
  for (std::uint32_t plane = 0; plane < total_planes; ++plane) {
    const auto it = plane_busy_ns_.find(plane);
    const SimTime busy = it == plane_busy_ns_.end() ? 0 : it->second;
    planes << plane << ',' << busy << ','
           << static_cast<double>(busy) / measurement_ns << '\n';
  }

  std::ofstream breakdown(std::filesystem::path(output_dir) /
                          "latency_breakdown.csv");
  breakdown << "op,samples,host_command_wait_ns,host_command_service_ns,"
               "host_data_wait_ns,host_data_service_ns,nand_queue_wait_ns,"
               "nand_command_wait_ns,array_service_ns,fabric_wait_ns,"
               "fabric_service_ns\n";
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
              << average(total.fabric_service_ns) << '\n';
  }

  std::ofstream source_breakdown(
      std::filesystem::path(output_dir) / "source_latency_breakdown.csv");
  source_breakdown
      << "source,op,samples,bytes,failed,host_command_wait_ns,"
         "host_command_service_ns,host_data_wait_ns,host_data_service_ns,"
         "nand_queue_wait_ns,nand_command_wait_ns,array_service_ns,"
         "fabric_wait_ns,fabric_service_ns\n";
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
                     << average(total.fabric_service_ns) << '\n';
  }

  const SimTime window_start = first_arrival_.value_or(0);
  const SimTime window_end = first_arrival_ ? last_completion_ : makespan;
  const auto merge = [window_start, window_end](const auto& intervals) {
    std::vector<std::pair<SimTime, SimTime>> clipped;
    for (const auto& interval : intervals) {
      const auto start = std::max(interval.start, window_start);
      const auto end = std::min(interval.end, window_end);
      if (end > start) clipped.emplace_back(start, end);
    }
    std::sort(clipped.begin(), clipped.end());
    std::vector<std::pair<SimTime, SimTime>> merged;
    for (const auto& interval : clipped) {
      if (merged.empty() || interval.first > merged.back().second)
        merged.push_back(interval);
      else
        merged.back().second = std::max(merged.back().second,
                                        interval.second);
    }
    return merged;
  };
  const auto duration = [](const auto& intervals) {
    SimTime total = 0;
    for (const auto& interval : intervals)
      total += interval.second - interval.first;
    return total;
  };
  const auto overlap_duration = [](const auto& left, const auto& right) {
    SimTime total = 0;
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < left.size() && j < right.size()) {
      const auto start = std::max(left[i].first, right[j].first);
      const auto end = std::min(left[i].second, right[j].second);
      if (end > start) total += end - start;
      if (left[i].second < right[j].second)
        ++i;
      else
        ++j;
    }
    return total;
  };
  const auto max_concurrency = [window_start, window_end](const auto& raw) {
    std::vector<std::pair<SimTime, int>> changes;
    for (const auto& interval : raw) {
      const auto start = std::max(interval.start, window_start);
      const auto end = std::min(interval.end, window_end);
      if (end > start) {
        changes.emplace_back(start, 1);
        changes.emplace_back(end, -1);
      }
    }
    std::sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) {
      return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    int active = 0;
    int maximum = 0;
    for (const auto& [_, delta] : changes) {
      active += delta;
      maximum = std::max(maximum, active);
    }
    return maximum;
  };

  std::ofstream resources(std::filesystem::path(output_dir) /
                          "resource_utilization.csv");
  resources << "stack,array_only_ns,fabric_only_ns,overlap_ns,idle_ns,"
               "array_utilization,fabric_utilization,host_utilization,"
               "avg_active_planes,max_active_planes\n";
  for (std::uint32_t stack = 0; stack < stacks_; ++stack) {
    const auto arrays = merge(stack_array_intervals_.at(stack));
    const auto fabric = merge(stack_fabric_intervals_.at(stack));
    const auto host = merge(stack_host_intervals_.at(stack));
    const auto array_ns = duration(arrays);
    const auto fabric_ns = duration(fabric);
    const auto host_ns = duration(host);
    const auto overlap_ns = overlap_duration(arrays, fabric);
    const auto union_ns = array_ns + fabric_ns - overlap_ns;
    SimTime plane_sum = 0;
    const auto first_plane = static_cast<std::uint64_t>(stack) *
                             dies_per_stack_ * planes_per_die_;
    const auto end_plane = first_plane +
                           static_cast<std::uint64_t>(dies_per_stack_) *
                               planes_per_die_;
    for (auto plane = first_plane; plane < end_plane; ++plane) {
      const auto it = plane_busy_ns_.find(static_cast<std::uint32_t>(plane));
      if (it != plane_busy_ns_.end()) plane_sum += it->second;
    }
    resources << stack << ',' << array_ns - overlap_ns << ','
              << fabric_ns - overlap_ns << ',' << overlap_ns << ','
              << (measurement_ns > union_ns ? measurement_ns - union_ns : 0)
              << ',' << static_cast<double>(array_ns) / measurement_ns << ','
              << static_cast<double>(fabric_ns) / measurement_ns << ','
              << static_cast<double>(host_ns) / measurement_ns << ','
              << static_cast<double>(plane_sum) / measurement_ns << ','
              << max_concurrency(stack_array_intervals_.at(stack)) << '\n';
  }

  std::ofstream ports(std::filesystem::path(output_dir) /
                      "data_port_utilization.csv");
  ports << "stack,port,busy_ns,utilization\n";
  for (std::uint32_t stack = 0; stack < stacks_; ++stack) {
    for (std::uint32_t port = 0; port < ports_per_stack_; ++port) {
      const auto intervals = merge(port_intervals_.at(
          static_cast<std::size_t>(stack) * ports_per_stack_ + port));
      const auto busy = duration(intervals);
      ports << stack << ',' << port << ',' << busy << ','
            << static_cast<double>(busy) / measurement_ns << '\n';
    }
  }

  std::ofstream dies(std::filesystem::path(output_dir) /
                     "die_utilization.csv");
  dies << "stack,die,busy_ns,utilization\n";
  for (std::uint32_t stack = 0; stack < stacks_; ++stack) {
    for (std::uint32_t die = 0; die < dies_per_stack_; ++die) {
      const auto intervals = merge(die_intervals_.at(
          static_cast<std::size_t>(stack) * dies_per_stack_ + die));
      const auto busy = duration(intervals);
      dies << stack << ',' << die << ',' << busy << ','
           << static_cast<double>(busy) / measurement_ns << '\n';
    }
  }

  std::ofstream hosts(std::filesystem::path(output_dir) /
                      "host_channel_utilization.csv");
  hosts << "stack,channel,busy_ns,utilization\n";
  for (std::uint32_t stack = 0; stack < stacks_; ++stack) {
    for (std::uint32_t channel = 0; channel < host_channels_per_stack_;
         ++channel) {
      const auto intervals = merge(host_channel_intervals_.at(
          static_cast<std::size_t>(stack) * host_channels_per_stack_ +
          channel));
      const auto busy = duration(intervals);
      hosts << stack << ',' << channel << ',' << busy << ','
            << static_cast<double>(busy) / measurement_ns << '\n';
    }
  }

  std::ofstream queues(std::filesystem::path(output_dir) /
                       "queue_depth.csv");
  queues << "time_ns,user_read,user_write,erase,refresh,active_planes\n";
  for (const auto& sample : queue_depth_samples_)
    queues << sample.time << ',' << sample.reads << ',' << sample.writes
           << ',' << sample.erases << ',' << sample.refreshes << ','
           << sample.active_planes << '\n';
}

}  // namespace hbfsim
