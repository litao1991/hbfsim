#include "hbfsim/kernel/event.h"

namespace hbfsim {

void EventQueue::schedule(SimTime when, EventType type,
                          std::uint64_t request_id,
                          std::uint64_t subrequest_id) {
  events_.push({when, next_sequence_++, type, request_id, subrequest_id});
}

Event EventQueue::pop() {
  const auto event = events_.top();
  events_.pop();
  return event;
}

}  // namespace hbfsim
