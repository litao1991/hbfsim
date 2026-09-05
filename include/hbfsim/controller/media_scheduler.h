#pragma once

#include "hbfsim/common/types.h"
#include "hbfsim/config/config.h"
#include "hbfsim/media/state.h"
#include "hbfsim/protocol/request.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace hbfsim {

class SchedulingPolicy {
 public:
  explicit SchedulingPolicy(const Config& config) : config_(config) {}
  int base_priority(const SubRequest& request) const;
  int priority(const SubRequest& request, SimTime waited,
               const PlaneControllerState& plane) const;

 private:
  const Config& config_;
};

class MediaScheduler {
 public:
  using Lookup = std::function<const SubRequest&(std::uint64_t)>;

  explicit MediaScheduler(const Config& config) : policy_(config) {}
  void enqueue(PlaneControllerState& plane, const SubRequest& request) const;
  void dequeue(PlaneControllerState& plane, const SubRequest& request,
               bool update_read_streak = true) const;
  std::optional<std::uint64_t> choose(const PlaneControllerState& plane,
                                      SimTime now,
                                      const Lookup& lookup) const;
  bool queues_empty(
      const PlaneControllerState::SourceQueues& queues) const;
  int base_priority(const SubRequest& request) const {
    return policy_.base_priority(request);
  }
  static std::size_t depth_index(OpType op);

 private:
  SchedulingPolicy policy_;
};

}  // namespace hbfsim
