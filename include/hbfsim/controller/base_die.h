#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/controller/interconnect_model.h"
#include "hbfsim/controller/media_scheduler.h"
#include "hbfsim/media/nand_media.h"

#include <cstdint>
#include <deque>
#include <limits>
#include <vector>

namespace hbfsim {

// Execution state belongs to the controller even while the Simulator remains
// the deterministic event kernel. Keeping it here prevents new scheduling
// features from adding device-resource state back to Simulator.
struct ControllerExecutionState {
  ControllerExecutionState(const Config& config, const NandTopology& topology);

  std::vector<std::uint32_t> active_per_die;
  std::vector<std::uint32_t> active_per_stack;
  std::vector<std::uint32_t> dispatch_cursor_per_stack;
  std::vector<SimTime> dispatch_wake_at;
  std::vector<std::deque<std::uint64_t>> program_ready;
};

class BaseDieController {
 public:
  BaseDieController(const Config& config, NandMediaSystem& media);

  MediaScheduler& scheduler() { return scheduler_; }
  const MediaScheduler& scheduler() const { return scheduler_; }
  InterconnectModel& interconnect() { return interconnect_; }
  const InterconnectModel& interconnect() const { return interconnect_; }
  ControllerExecutionState& execution() { return execution_; }
  const ControllerExecutionState& execution() const { return execution_; }
  PlaneControllerState& plane_state(const PhysicalAddr& address);
  const PlaneControllerState& plane_state(
      const PhysicalAddr& address) const;

  SimTime command_ready_time(const SubRequest& request) const;
  void claim_command(const SubRequest& request, SimTime now,
                     bool shared_command);

 private:
  const Config& config_;
  NandMediaSystem& media_;
  MediaScheduler scheduler_;
  InterconnectModel interconnect_;
  ControllerExecutionState execution_;
  std::vector<PlaneControllerState> plane_states_;
};

}  // namespace hbfsim
