#include "hbfsim/core.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hbfsim {
namespace {

bool bitmap_test(const std::vector<std::uint64_t>& bitmap,
                 std::uint32_t page) {
  const auto word = page / 64;
  return word < bitmap.size() &&
         (bitmap[word] & (1ULL << (page % 64))) != 0;
}



}  // namespace

Simulator::Simulator(Config config)
    : config_(std::move(config)), mapper_(config_), reliability_(config_) {
  config_.validate();
  const auto planes_per_stack =
      static_cast<std::uint64_t>(config_.dies_per_stack) *
      config_.planes_per_die;
  const auto total_planes =
      static_cast<std::uint64_t>(config_.stacks) * planes_per_stack;
  planes_.resize(static_cast<std::size_t>(total_planes));
  for (auto& target : planes_)
    target.blocks.resize(config_.blocks_per_plane);
  dies_.resize(static_cast<std::size_t>(config_.stacks) *
               config_.dies_per_stack);
  active_per_die_.assign(dies_.size(), 0);
  active_per_stack_.assign(config_.stacks, 0);
  dispatch_cursor_per_stack_.assign(config_.stacks, 0);
  dispatch_wake_at_.assign(config_.stacks,
                           std::numeric_limits<SimTime>::max());
  program_ready_.resize(config_.stacks);
  stats_.set_total_planes(static_cast<std::uint32_t>(total_planes));

  host_links_.reserve(static_cast<std::size_t>(config_.stacks) *
                      config_.host_channels_per_stack);
  for (std::uint64_t i = 0;
       i < static_cast<std::uint64_t>(config_.stacks) *
               config_.host_channels_per_stack;
       ++i)
    host_links_.emplace_back(config_.host_bw_bytes_per_ns,
                             config_.host_fixed_latency_ns);
  for (std::uint32_t i = 0; i < config_.stacks; ++i)
    fabrics_.emplace_back(config_.ports_per_stack,
                          config_.internal_bw_bytes_per_ns,
                          config_.internal_fixed_latency_ns);
}

void Simulator::schedule(SimTime when, EventType type,
                         std::uint64_t request_id,
                         std::uint64_t subrequest_id) {
  events_.push({when, next_event_seq_++, type, request_id, subrequest_id});
}

void Simulator::schedule_dispatch_wake(std::uint32_t stack, SimTime when) {
  if (when <= now_) return;
  auto& scheduled = dispatch_wake_at_.at(stack);
  if (when >= scheduled) return;
  scheduled = when;
  schedule(when, EventType::DispatchWake, 0, stack);
}

void Simulator::submit(const TraceEntry& entry) {
  if (config_.max_requests &&
      submitted_requests_ >= config_.max_requests)
    return;
  if (entry.timestamp_ns < now_)
    throw std::invalid_argument("cannot submit an event in simulated past");
  if ((entry.op == OpType::Read || entry.op == OpType::Write) &&
      entry.size == 0)
    throw std::invalid_argument("read/write request size must be non-zero");
  Request request{next_request_id_++, entry.timestamp_ns, entry.op,
                  entry.address, entry.size, entry.stream};
  requests_.emplace(request.id, request);
  ++submitted_requests_;
  schedule(entry.timestamp_ns, EventType::HostArrival, request.id);
}

std::uint32_t Simulator::plane_index(const PhysicalAddr& address) const {
  return mapper_.flat_plane(address);
}

Plane& Simulator::plane(const PhysicalAddr& address) {
  return planes_.at(plane_index(address));
}

const Plane& Simulator::plane(const PhysicalAddr& address) const {
  return planes_.at(plane_index(address));
}

DieState& Simulator::die(const PhysicalAddr& address) {
  return dies_.at(static_cast<std::size_t>(address.stack) *
                      config_.dies_per_stack +
                  address.die);
}

const DieState& Simulator::die(const PhysicalAddr& address) const {
  return dies_.at(static_cast<std::size_t>(address.stack) *
                      config_.dies_per_stack +
                  address.die);
}

std::uint64_t Simulator::page_key(const PhysicalAddr& address) const {
  std::uint64_t key = address.stack;
  key = key * config_.dies_per_stack + address.die;
  key = key * config_.planes_per_die + address.plane;
  key = key * config_.blocks_per_plane + address.block;
  return key * config_.pages_per_block + address.page;
}

PageState Simulator::page_state(const PhysicalAddr& address) const {
  if (const auto it = transient_page_states_.find(page_key(address));
      it != transient_page_states_.end())
    return it->second;
  const auto& block = plane(address).blocks.at(address.block);
  if (bitmap_test(block.failed_bitmap, address.page))
    return PageState::Failed;
  if (bitmap_test(block.valid_bitmap, address.page))
    return PageState::Valid;
  if (bitmap_test(block.invalid_bitmap, address.page))
    return PageState::Invalid;
  return PageState::Erased;
}

BlockState Simulator::block_state(const PhysicalAddr& address) const {
  return plane(address).blocks.at(address.block).state;
}

SimTime Simulator::block_ready_at(const PhysicalAddr& address) const {
  return plane(address).blocks.at(address.block).ready_at;
}

SimTime Simulator::die_ready_at(const PhysicalAddr& address) const {
  const auto& state = die(address);
  return std::max(state.ready_at, state.command_ready_at);
}

void Simulator::set_transient_page_state(const PhysicalAddr& address,
                                         PageState state) {
  transient_page_states_[page_key(address)] = state;
}

void Simulator::clear_transient_page_state(const PhysicalAddr& address) {
  transient_page_states_.erase(page_key(address));
}

SimTime Simulator::reserve_host(const PhysicalAddr& address, SimTime now,
                                std::uint64_t bytes) {
  const auto channel =
      address.data_port % config_.host_channels_per_stack;
  const auto index = static_cast<std::size_t>(address.stack) *
                         config_.host_channels_per_stack +
                     channel;
  return host_links_.at(index).reserve(now, bytes);
}

SimTime Simulator::reserve_fabric(const PhysicalAddr& address, SimTime now,
                                  std::uint64_t bytes) {
  return fabrics_.at(address.stack).reserve(now, bytes, address.data_port);
}

void Simulator::split_request(Request& request) {
  const auto first_lpn = request.logical_addr / config_.page_size;
  if (request.op == OpType::Erase || request.op == OpType::Refresh) {
    const auto address = mapper_.map_read(first_lpn);
    SubRequest sub{next_subrequest_id_++, request.id, request.op,
                   TransactionSource::Maintenance, first_lpn, 0, address,
                   std::nullopt};
    sub.arrival_time = now_;
    subrequests_.emplace(sub.id, sub);
    ++request.pending_subreqs;
    schedule(now_, EventType::SubreqReady, request.id, sub.id);
    return;
  }

  const auto offset = request.logical_addr % config_.page_size;
  std::uint64_t remaining = request.size;
  std::uint64_t lpn = first_lpn;
  std::uint64_t first_offset = offset;
  while (remaining > 0) {
    const auto bytes = std::min<std::uint64_t>(
        remaining, config_.page_size - first_offset);
    PhysicalAddr address = request.op == OpType::Write
                               ? mapper_.placement(lpn)
                               : mapper_.map_read(lpn);
    address.offset = first_offset;
    SubRequest sub{next_subrequest_id_++, request.id, request.op,
                   TransactionSource::User, lpn, bytes, address,
                   std::nullopt};
    sub.arrival_time = now_;
    subrequests_.emplace(sub.id, sub);
    ++request.pending_subreqs;
    const auto ready = request.op == OpType::Write
                           ? reserve_host(address, now_, bytes)
                           : now_;
    schedule(ready, EventType::SubreqReady, request.id, sub.id);
    remaining -= bytes;
    ++lpn;
    first_offset = 0;
  }
}

void Simulator::run() {
  while (!events_.empty()) {
    const Event event = events_.top();
    events_.pop();
    now_ = event.time;
    handle(event);
  }
}

void Simulator::run_until(SimTime until) {
  if (until < now_)
    throw std::invalid_argument("run_until cannot move simulated time backward");
  while (!events_.empty() && events_.top().time <= until) {
    const Event event = events_.top();
    events_.pop();
    now_ = event.time;
    handle(event);
  }
  now_ = until;
}

}  // namespace hbfsim
