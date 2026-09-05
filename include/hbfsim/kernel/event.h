#pragma once

#include "hbfsim/common/types.h"
#include <cstdint>
#include <queue>
#include <vector>

namespace hbfsim {

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

}  // namespace hbfsim
