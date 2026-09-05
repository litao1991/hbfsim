#include "hbfsim/core.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace hbfsim {

void StatsCollector::record_request(const Request& request) {
  ++completed_requests_;
  if (request.failed) ++failed_requests_;
  if (request.op == OpType::Read) ++read_requests_;
  if (request.op == OpType::Write) ++write_requests_;
  completed_bytes_ += request.size;
  if (!request.failed) successful_bytes_ += request.size;
  op_bytes_[request.op] += request.size;
  if (!first_arrival_ || request.arrival_time < *first_arrival_)
    first_arrival_ = request.arrival_time;
  last_completion_ = std::max(last_completion_, request.complete_time);
  latencies_.push_back(request.complete_time - request.arrival_time);
  op_latencies_[request.op].push_back(request.complete_time - request.arrival_time);
}

void StatsCollector::record_subrequest(const SubRequest& subrequest) {
  subrequest_waits_.push_back(subrequest.issue_time - subrequest.enqueue_time);
}

void StatsCollector::record_plane_issue(std::uint32_t plane, SimTime start, SimTime end) {
  plane_busy_ns_[plane] += end - start;
}

double StatsCollector::mean_latency_ns() const {
  if (latencies_.empty()) return 0.0;
  return static_cast<double>(std::accumulate(latencies_.begin(), latencies_.end(), SimTime{0})) / latencies_.size();
}

double StatsCollector::p99_latency_ns() const {
  if (latencies_.empty()) return 0.0;
  auto ordered = latencies_;
  std::sort(ordered.begin(), ordered.end());
  const auto index = std::min<std::size_t>(ordered.size() - 1,
      static_cast<std::size_t>(std::ceil(ordered.size() * 0.99) - 1));
  return static_cast<double>(ordered[index]);
}

void StatsCollector::write(const std::string& output_dir, SimTime makespan) const {
  std::filesystem::create_directories(output_dir);
  std::ofstream summary(std::filesystem::path(output_dir) / "summary.csv");
  if (!summary) throw std::runtime_error("cannot write results");
  const SimTime measurement_ns = first_arrival_
      ? std::max<SimTime>(1, last_completion_ - *first_arrival_)
      : std::max<SimTime>(1, makespan);
  const double seconds = static_cast<double>(measurement_ns) / 1e9;
  summary << "metric,value\ncompleted_requests," << completed_requests_ << "\nread_requests," << read_requests_
          << "\nwrite_requests," << write_requests_ << "\ncompleted_bytes," << completed_bytes_
          << "\nsuccessful_bytes," << successful_bytes_
          << "\nfailed_requests," << failed_requests_
          << "\nprogram_failures," << program_failures_
          << "\ncorrected_reads," << corrected_reads_
          << "\nuncorrectable_reads," << uncorrectable_reads_
          << "\nread_retries," << read_retries_
          << "\nmakespan_ns," << makespan << "\nmeasurement_duration_ns," << measurement_ns
          << "\nmean_latency_ns," << mean_latency_ns()
          << "\np99_latency_ns," << p99_latency_ns() << "\neffective_bandwidth_GBps,"
          << (static_cast<double>(completed_bytes_) / seconds / 1e9)
          << "\neffective_goodput_GBps,"
          << (static_cast<double>(successful_bytes_) / seconds / 1e9) << "\n";
  for (const auto& [op, values] : op_latencies_) {
    if (values.empty()) continue;
    auto ordered = values;
    std::sort(ordered.begin(), ordered.end());
    const auto index = std::min<std::size_t>(ordered.size() - 1,
        static_cast<std::size_t>(std::ceil(ordered.size() * 0.99) - 1));
    const auto mean = static_cast<double>(std::accumulate(values.begin(), values.end(), SimTime{0})) / values.size();
    summary << "op_" << to_string(op) << "_count," << values.size() << "\n"
            << "op_" << to_string(op) << "_bytes," << op_bytes_.at(op) << "\n"
            << "op_" << to_string(op) << "_bandwidth_GBps,"
            << (static_cast<double>(op_bytes_.at(op)) / seconds / 1e9) << "\n"
            << "op_" << to_string(op) << "_mean_latency_ns," << mean << "\n"
            << "op_" << to_string(op) << "_p99_latency_ns," << ordered[index] << "\n";
  }
  std::ofstream planes(std::filesystem::path(output_dir) / "plane_utilization.csv");
  planes << "plane,busy_ns,utilization\n";
  for (std::uint32_t plane = 0; plane < total_planes_; ++plane) {
    const auto it = plane_busy_ns_.find(plane);
    const SimTime busy = it == plane_busy_ns_.end() ? 0 : it->second;
    planes << plane << ',' << busy << ',' << static_cast<double>(busy) / measurement_ns << '\n';
  }
}

}  // namespace hbfsim
