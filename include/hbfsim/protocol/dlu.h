#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/protocol/channel.h"
#include "hbfsim/protocol/status.h"
#include <cstdint>
#include <cstddef>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace hbfsim {

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

}  // namespace hbfsim
