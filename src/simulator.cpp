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

void bitmap_set(std::vector<std::uint64_t>& bitmap, std::uint32_t pages,
                std::uint32_t page) {
  if (bitmap.empty()) bitmap.resize((pages + 63) / 64, 0);
  bitmap.at(page / 64) |= 1ULL << (page % 64);
}

void bitmap_clear(std::vector<std::uint64_t>& bitmap, std::uint32_t page) {
  if (!bitmap.empty()) bitmap.at(page / 64) &= ~(1ULL << (page % 64));
}

}  // namespace

Simulator::Simulator(Config config)
    : config_(std::move(config)),
      mapper_(config_),
      host_router_(config_),
      reliability_(config_),
      host_gc_manager_(config_),
      refresh_manager_(config_) {
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
  stats_.set_topology(config_.stacks, config_.dies_per_stack,
                      config_.planes_per_die, config_.ports_per_stack,
                      config_.host_channels_per_stack);
  const auto physical_pages =
      static_cast<std::uint64_t>(config_.stacks) * config_.dies_per_stack *
      config_.planes_per_die * config_.blocks_per_plane *
      config_.pages_per_block;
  const auto host_visible_pages = mapper_.stripe_mapping()
      ? static_cast<std::uint64_t>(
            mapper_.stripe_mapping()->host_visible_stripe_count()) *
            mapper_.stripe_mapping()->stripe_capacity()
      : physical_pages;
  stats_.set_capacity(physical_pages * config_.page_size,
                      host_visible_pages * config_.page_size);

  host_interfaces_.reserve(config_.stacks);
  for (std::uint32_t i = 0; i < config_.stacks; ++i) {
    host_interfaces_.emplace_back(config_.host_channels_per_stack,
                                  config_.host_bw_bytes_per_ns,
                                  config_.host_fixed_latency_ns,
                                  config_.host_full_duplex);
    fabrics_.emplace_back(config_.ports_per_stack,
                          config_.internal_bw_bytes_per_ns,
                          config_.internal_port_bw_bytes_per_ns,
                          config_.internal_fixed_latency_ns);
  }
}

void Simulator::schedule(SimTime when, EventType type,
                         std::uint64_t request_id,
                         std::uint64_t subrequest_id) {
  event_queue_.schedule(when, type, request_id, subrequest_id);
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
  if (entry.op == OpType::Invalidate &&
      (entry.size == 0 || entry.address % config_.page_size != 0 ||
       entry.size % config_.page_size != 0))
    throw std::invalid_argument(
        "invalidate range must be non-empty and page-aligned");
  Request request;
  request.id = next_request_id_++;
  request.arrival_time = entry.timestamp_ns;
  request.op = entry.op;
  request.logical_addr = entry.address;
  request.size = entry.size;
  request.stream_id = entry.stream;
  request.measured = streaming_submission_
                         ? phase_ == SimulationPhase::Measure
                         : request.id >= config_.warmup_requests;
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

std::uint64_t Simulator::block_key(const PhysicalAddr& address) const {
  std::uint64_t key = address.stack;
  key = key * config_.dies_per_stack + address.die;
  key = key * config_.planes_per_die + address.plane;
  return key * config_.blocks_per_plane + address.block;
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

std::uint32_t Simulator::block_erase_count(
    const PhysicalAddr& address) const {
  return plane(address).blocks.at(address.block).erase_count;
}

void Simulator::retire_block(const PhysicalAddr& address) {
  auto& block = plane(address).blocks.at(address.block);
  const bool newly_bad = !block.bad;
  if (newly_bad) {
    block.bad = true;
    block.state = BlockState::Bad;
    stats_.record_retired_block();
  }
  if (auto* mapping = mapper_.stripe_mapping();
      mapping && mapping->retire_stripe(address)) {
    const auto loss = static_cast<std::uint64_t>(mapping->stripe_capacity()) *
                      config_.page_size;
    stats_.record_retired_stripe(loss);
    host_gc_manager_.notify_media_change();
  } else if (!mapping && newly_bad) {
    const auto loss = static_cast<std::uint64_t>(config_.pages_per_block) *
                      config_.page_size;
    stats_.record_retired_stripe(loss);
  }
}

SimTime Simulator::die_ready_at(const PhysicalAddr& address) const {
  const auto& state = die(address);
  return std::max(state.ready_at, state.command_ready_at);
}

void Simulator::invalidate_host_page(std::uint64_t logical_addr) {
  auto* mapping = mapper_.stripe_mapping();
  if (!mapping)
    throw std::runtime_error("INVALIDATE_REQUIRES_HOST_MANAGED_MAPPING");
  if (logical_addr % config_.page_size != 0)
    throw std::invalid_argument("INVALIDATE_REQUIRES_PAGE_ALIGNMENT");
  const auto lpn = logical_addr / config_.page_size;
  const auto paddr = mapping->lookup(lpn);
  if (!paddr) throw std::runtime_error("INVALIDATE_UNMAPPED_LPN");
  mapping->invalidate(lpn);
  host_gc_manager_.notify_media_change();
  auto& block = plane(*paddr).blocks.at(paddr->block);
  bitmap_clear(block.valid_bitmap, paddr->page);
  bitmap_set(block.invalid_bitmap, config_.pages_per_block, paddr->page);
  --block.valid_pages;
  ++block.invalid_pages;
}

void Simulator::set_transient_page_state(const PhysicalAddr& address,
                                         PageState state) {
  transient_page_states_[page_key(address)] = state;
}

void Simulator::clear_transient_page_state(const PhysicalAddr& address) {
  transient_page_states_.erase(page_key(address));
}

LinkResource::Reservation Simulator::reserve_host(
    const HostRoute& route, HostLinkDirection direction, SimTime now,
    std::uint64_t bytes, bool measured) {
  auto reservation =
      host_interfaces_.at(route.stack).reserve(route, direction, now, bytes);
  if (measured)
    stats_.record_host_transfer(route.stack, route.channel, direction,
                                reservation.start, reservation.transfer_end);
  return reservation;
}

LinkResource::Reservation Simulator::reserve_fabric(
    const PhysicalAddr& address, SimTime now, std::uint64_t bytes,
    bool measured) {
  auto reservation = fabrics_.at(address.stack).reserve_window(
      now, bytes, address.data_port);
  if (measured)
    stats_.record_fabric_transfer(address.stack, address.data_port,
                                  reservation.start,
                                  reservation.transfer_end);
  return reservation;
}

void Simulator::split_request(Request& request) {
  const auto first_lpn = request.logical_addr / config_.page_size;
  if (request.op == OpType::Invalidate) {
    const auto page_count = request.size / config_.page_size;
    for (std::uint64_t page = 0; page < page_count; ++page)
      invalidate_host_page((first_lpn + page) * config_.page_size);
    request.complete_time = now_;
    if (request.measured) stats_.record_request(request);
    requests_.erase(request.id);
    return;
  }
  if (request.op == OpType::Erase || request.op == OpType::Refresh) {
    const auto address = mapper_.map_read(first_lpn);
    const auto add_subrequest = [&](const PhysicalAddr& paddr) {
      SubRequest sub;
      sub.id = next_subrequest_id_++;
      sub.parent_id = request.id;
      sub.op = request.op;
      sub.source = TransactionSource::Maintenance;
      sub.lpn = first_lpn;
      sub.paddr = paddr;
      sub.arrival_time = now_;
      sub.host_route = request.host_route;
      sub.latency.host_command_wait_ns = request.host_command_wait_ns;
      sub.latency.host_command_service_ns = request.host_command_service_ns;
      subrequests_.emplace(sub.id, sub);
      ++request.pending_subreqs;
      schedule(now_, EventType::SubreqReady, request.id, sub.id);
    };
    if (request.op == OpType::Erase && mapper_.stripe_mapping()) {
      const auto active = mapper_.stripe_mapping()->active_stripe(first_lpn);
      if (!active) {
        add_subrequest(address);
      } else {
        for (std::uint32_t lane = 0;
             lane < mapper_.stripe_mapping()->stripe_width(); ++lane)
          add_subrequest(mapper_.stripe_mapping()->address_for(*active, lane));
      }
    } else {
      add_subrequest(address);
    }
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
                               ? mapper_.prepare_write(lpn)
                               : mapper_.map_read(lpn);
    address.offset = first_offset;
    if (request.op == OpType::Read &&
        config_.initialization_mode != InitializationMode::Empty)
      materialize_initialized_page(address);
    SubRequest sub;
    sub.id = next_subrequest_id_++;
    sub.parent_id = request.id;
    sub.op = request.op;
    sub.source = TransactionSource::User;
    sub.lpn = lpn;
    sub.bytes = bytes;
    sub.paddr = address;
    sub.arrival_time = now_;
    sub.host_route = host_router_.route(lpn * config_.page_size, address);
    sub.latency.host_command_wait_ns = request.host_command_wait_ns;
    sub.latency.host_command_service_ns = request.host_command_service_ns;
    ++request.pending_subreqs;
    auto ready = now_;
    if (request.op == OpType::Write) {
      const auto transfer = reserve_host(
          sub.host_route, HostLinkDirection::HostToDevice, now_, bytes,
          request.measured);
      sub.latency.host_data_wait_ns = transfer.start - now_;
      sub.latency.host_data_service_ns = transfer.completion - transfer.start;
      ready = transfer.completion;
    }
    subrequests_.emplace(sub.id, sub);
    schedule(ready, EventType::SubreqReady, request.id, sub.id);
    remaining -= bytes;
    ++lpn;
    first_offset = 0;
  }
}

void Simulator::run() {
  maybe_start_host_gc(now_);
  maybe_start_automatic_refresh(now_);
  while (!event_queue_.empty()) {
    const Event event = event_queue_.pop();
    now_ = event.time;
    handle(event);
    maybe_start_host_gc(now_);
    maybe_start_automatic_refresh(now_);
  }
  phase_ = SimulationPhase::Drain;
}

void Simulator::run(IRequestSource& source) {
  if (!event_queue_.empty() || submitted_requests_ != 0)
    throw std::logic_error(
        "streaming run requires a fresh simulator with no submitted requests");
  streaming_submission_ = true;
  phase_ = config_.warmup_requests ? SimulationPhase::Warmup
                                   : SimulationPhase::Measure;

  TraceEntry entry{};
  for (std::uint64_t i = 0; i < config_.warmup_requests; ++i) {
    if (config_.max_requests && submitted_requests_ >= config_.max_requests)
      break;
    if (!source.next(entry)) break;
    submit(entry);
  }
  while (!event_queue_.empty()) {
    const auto event = event_queue_.pop();
    now_ = event.time;
    handle(event);
    maybe_start_host_gc(now_);
    maybe_start_automatic_refresh(now_);
  }

  phase_ = SimulationPhase::Measure;
  bool has_next =
      (!config_.max_requests || submitted_requests_ < config_.max_requests) &&
      source.next(entry);
  SimTime measurement_offset = 0;
  if (has_next && entry.timestamp_ns < now_)
    measurement_offset = now_ - entry.timestamp_ns;
  if (has_next) next_trace_arrival_ = entry.timestamp_ns + measurement_offset;

  while (has_next || !event_queue_.empty()) {
    const SimTime adjusted_time = has_next
                                      ? entry.timestamp_ns + measurement_offset
                                      : std::numeric_limits<SimTime>::max();
    if (has_next &&
        (event_queue_.empty() || adjusted_time < event_queue_.next().time)) {
      auto adjusted = entry;
      adjusted.timestamp_ns = adjusted_time;
      submit(adjusted);
      has_next =
          (!config_.max_requests ||
           submitted_requests_ < config_.max_requests) &&
          source.next(entry);
      next_trace_arrival_ = has_next
                                ? std::optional<SimTime>(
                                      entry.timestamp_ns + measurement_offset)
                                : std::nullopt;
      continue;
    }
    const auto event = event_queue_.pop();
    now_ = event.time;
    handle(event);
    maybe_start_host_gc(now_);
    maybe_start_automatic_refresh(now_);
  }
  next_trace_arrival_.reset();
  phase_ = SimulationPhase::Drain;
  streaming_submission_ = false;
}

void Simulator::run_until(SimTime until) {
  if (until < now_)
    throw std::invalid_argument("run_until cannot move simulated time backward");
  maybe_start_host_gc(now_);
  maybe_start_automatic_refresh(now_);
  while (!event_queue_.empty() && event_queue_.next().time <= until) {
    const Event event = event_queue_.pop();
    now_ = event.time;
    handle(event);
    maybe_start_host_gc(now_);
    maybe_start_automatic_refresh(now_);
  }
  now_ = until;
}

void Simulator::materialize_initialized_page(const PhysicalAddr& address) {
  auto& block = plane(address).blocks.at(address.block);
  if (erased_blocks_.contains(block_key(address)) || block.bad ||
      bitmap_test(block.valid_bitmap, address.page) ||
      page_state(address) != PageState::Erased)
    return;
  bitmap_set(block.valid_bitmap, config_.pages_per_block, address.page);
  ++block.valid_pages;
  block.next_program_page =
      std::max(block.next_program_page, address.page + 1);
  block.state = block.next_program_page == config_.pages_per_block
                    ? BlockState::Closed
                    : BlockState::Open;
}

bool Simulator::is_measured(std::uint64_t request_id) const {
  return requests_.at(request_id).measured;
}

void Simulator::record_queue_depth() {
  if (phase_ != SimulationPhase::Measure) return;
  std::uint64_t active = 0;
  for (const auto value : active_per_stack_) active += value;
  stats_.record_queue_depth(now_, queue_depth_[0], queue_depth_[1],
                            queue_depth_[2], queue_depth_[3], active);
}

}  // namespace hbfsim
