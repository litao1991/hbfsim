#include "hbfsim/simulator.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace hbfsim {
Simulator::Simulator(Config config)
    : config_(std::move(config)),
      system_(config_) {
  config_.validate();
  stats_.set_topology(config_.stacks, config_.dies_per_stack,
                      config_.planes_per_die, config_.ports_per_stack,
                      config_.host_channels_per_stack);
  stats_.set_queue_depth_sample_interval(
      config_.queue_depth_sample_interval_ns);
  const auto physical_pages =
      static_cast<std::uint64_t>(config_.stacks) * config_.dies_per_stack *
      config_.planes_per_die * config_.blocks_per_plane *
      config_.pages_per_block;
  const auto host_visible_pages = system_.mapper().stripe_mapping()
      ? static_cast<std::uint64_t>(
            system_.mapper().stripe_mapping()->host_visible_stripe_count()) *
            system_.mapper().stripe_mapping()->stripe_capacity()
      : physical_pages;
  stats_.set_capacity(physical_pages * config_.page_size,
                      host_visible_pages * config_.page_size);
  if (const auto* mapping = system_.mapper().stripe_mapping())
    stats_.set_stripe_geometry(mapping->parallelism_group_count(),
                               mapping->stripe_width(),
                               mapping->stripe_capacity());
}

void Simulator::schedule(SimTime when, EventType type,
                         std::uint64_t request_id,
                         std::uint64_t subrequest_id) {
  event_queue_.schedule(when, type, request_id, subrequest_id);
}

void Simulator::schedule_dispatch_wake(std::uint32_t stack, SimTime when) {
  if (when <= now_) return;
  auto& scheduled = system_.controller().execution().dispatch_wake_at.at(stack);
  if (when >= scheduled) return;
  scheduled = when;
  schedule(when, EventType::DispatchWake, 0, stack);
}

void Simulator::hold_batch_read(SubRequest& subrequest, SimTime now) {
  const auto bank = system_.topology().flat_bank(subrequest.paddr);
  auto& bucket = batch_reads_[bank];
  if (!bucket.has_address) {
    bucket.address = subrequest.paddr;
    bucket.has_address = true;
  }
  bucket.pending.push_back(subrequest.id);
  if (bucket.emit_at != std::numeric_limits<SimTime>::max()) return;
  bucket.emit_at = now + config_.batch_read_aggregation_window_ns;
  schedule(bucket.emit_at, EventType::BatchReadEmit, 0, bank);
}

void Simulator::emit_batch_reads(std::uint32_t bank, SimTime now) {
  const auto found = batch_reads_.find(bank);
  if (found == batch_reads_.end()) return;
  auto& bucket = found->second;
  if (bucket.emit_at != now) return;
  bucket.emit_at = std::numeric_limits<SimTime>::max();
  if (bucket.pending.empty()) return;

  const auto first_id = bucket.pending.front();
  const auto address = subrequests_.at(first_id).paddr;
  auto& sense = system_.media().sense_queue(address);
  const bool idle = sense.empty();
  const auto count = std::min<std::size_t>(
      bucket.pending.size(), config_.batch_read_max_pages);
  SimTime accumulated_delay = 0;
  for (std::size_t page = 0; page < count; ++page) {
    const auto id = bucket.pending.front();
    bucket.pending.pop_front();
    auto& subrequest = subrequests_.at(id);
    sense.enqueue(id);
    accumulated_delay += now - subrequest.arrival_time;
  }
  if (is_measured(subrequests_.at(first_id).parent_id))
    stats_.record_batch_read(count, accumulated_delay);
  if (idle) release_next_batch_read(bank, now);
  if (!bucket.pending.empty()) {
    bucket.emit_at = now + config_.batch_read_aggregation_window_ns;
    schedule(bucket.emit_at, EventType::BatchReadEmit, 0, bank);
  }
}

void Simulator::release_next_batch_read(std::uint32_t bank, SimTime now) {
  const auto found = batch_reads_.find(bank);
  if (found == batch_reads_.end()) return;
  auto& bucket = found->second;
  if (!bucket.has_address) return;
  auto& sense = system_.media().sense_queue(bucket.address);
  if (sense.empty()) return;
  auto& subrequest = subrequests_.at(sense.front());
  if (subrequest.batch_released) return;
  subrequest.batch_released = true;
  subrequest.batch_sense_held = true;
  schedule(now, EventType::SubreqReady, subrequest.parent_id, subrequest.id);
}

void Simulator::complete_batch_sense(SubRequest& subrequest, SimTime now) {
  if (!subrequest.batch_sense_held) return;
  const auto bank = system_.topology().flat_bank(subrequest.paddr);
  auto& sense = system_.media().sense_queue(subrequest.paddr);
  sense.pop_front(subrequest.id);
  subrequest.batch_sense_held = false;
  release_next_batch_read(bank, now);
}

void Simulator::submit(const TraceEntry& entry) {
  if (config_.max_requests &&
      submitted_requests_ >= config_.max_requests)
    return;
  if (entry.timestamp_ns < now_)
    throw std::invalid_argument("cannot submit an event in simulated past");

  const auto request_id = next_request_id_++;
  const bool measured =
      streaming_submission_ ? phase_ == SimulationPhase::Measure
                            : request_id >= config_.warmup_requests;
  auto admission = system_.frontend().admit(entry, request_id, measured);
  ++submitted_requests_;
  if (!admission.accepted()) {
    if (measured) stats_.record_request(admission.request);
    responses_.push_back(std::move(*admission.rejection));
    return;
  }

  const auto accepted_id = admission.request.id;
  requests_.emplace(accepted_id, std::move(admission.request));
  schedule(entry.timestamp_ns, EventType::HostArrival, accepted_id);
}

void Simulator::publish_response(const Request& request) {
  auto released = system_.frontend().complete(request);
  responses_.insert(responses_.end(),
                    std::make_move_iterator(released.begin()),
                    std::make_move_iterator(released.end()));
}

std::uint32_t Simulator::plane_index(const PhysicalAddr& address) const {
  return system_.topology().flat_plane(address);
}

const PlaneMediaState& Simulator::media_plane(
    const PhysicalAddr& address) const {
  return system_.media().plane(address);
}

PlaneControllerState& Simulator::controller_plane(
    const PhysicalAddr& address) {
  return system_.controller().plane_state(address);
}

const PlaneControllerState& Simulator::controller_plane(
    const PhysicalAddr& address) const {
  return system_.controller().plane_state(address);
}

DieState& Simulator::die(const PhysicalAddr& address) {
  return system_.media().die(address);
}

const DieState& Simulator::die(const PhysicalAddr& address) const {
  return system_.media().die(address);
}

BankState& Simulator::bank(const PhysicalAddr& address) {
  return system_.media().bank(address);
}

const BankState& Simulator::bank(const PhysicalAddr& address) const {
  return system_.media().bank(address);
}

PageState Simulator::page_state(const PhysicalAddr& address) const {
  return system_.media().page_state(address);
}

BlockState Simulator::block_state(const PhysicalAddr& address) const {
  return system_.media().block_state(address);
}

SimTime Simulator::block_ready_at(const PhysicalAddr& address) const {
  return system_.media().block_ready_at(address);
}

std::uint32_t Simulator::block_erase_count(
    const PhysicalAddr& address) const {
  return system_.media().block_erase_count(address);
}

void Simulator::retire_block(const PhysicalAddr& address) {
  const bool newly_bad = system_.media().retire_block(address);
  if (newly_bad) stats_.record_retired_block();
  if (auto* mapping = system_.mapper().stripe_mapping();
      mapping && mapping->retire_stripe(address)) {
    const auto loss = static_cast<std::uint64_t>(mapping->stripe_capacity()) *
                      config_.page_size;
    stats_.record_retired_stripe(loss);
    system_.host_gc_manager().notify_media_change();
  } else if (!mapping && newly_bad) {
    const auto loss = static_cast<std::uint64_t>(config_.pages_per_block) *
                      config_.page_size;
    stats_.record_capacity_loss(loss);
  }
}

SimTime Simulator::die_ready_at(const PhysicalAddr& address) const {
  return system_.media().die_ready_at(address);
}

void Simulator::invalidate_host_page(std::uint64_t logical_addr) {
  auto* mapping = system_.mapper().stripe_mapping();
  if (!mapping)
    throw std::runtime_error("INVALIDATE_REQUIRES_HOST_MANAGED_MAPPING");
  if (logical_addr % config_.page_size != 0)
    throw std::invalid_argument("INVALIDATE_REQUIRES_PAGE_ALIGNMENT");
  const auto lpn = logical_addr / config_.page_size;
  const auto paddr = mapping->lookup(lpn);
  if (!paddr) throw std::runtime_error("INVALIDATE_UNMAPPED_LPN");
  mapping->invalidate(lpn);
  system_.media().invalidate_read_cache_page(*paddr);
  system_.host_gc_manager().notify_media_change();
  system_.media().invalidate_page(*paddr);
}

LinkResource::Reservation Simulator::reserve_host(
    const HostRoute& route, HostLinkDirection direction, SimTime now,
    std::uint64_t bytes, bool measured) {
  auto reservation =
      system_.controller().interconnect().reserve_host(route, direction, now, bytes);
  if (measured && reservation.transfer_end > reservation.start) {
    schedule(reservation.start, EventType::ResourceHostStart,
             route.stack, route.channel);
    schedule(reservation.transfer_end, EventType::ResourceHostEnd,
             route.stack, route.channel);
  }
  return reservation;
}

LinkResource::Reservation Simulator::reserve_fabric(
    const PhysicalAddr& address, SimTime now, std::uint64_t bytes,
    bool measured) {
  auto reservation =
      system_.controller().interconnect().reserve_fabric(address, now, bytes);
  if (measured && reservation.transfer_end > reservation.start) {
    schedule(reservation.start, EventType::ResourceFabricStart,
             address.stack, address.data_port);
    schedule(reservation.transfer_end, EventType::ResourceFabricEnd,
             address.stack, address.data_port);
  }
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
    publish_response(request);
    requests_.erase(request.id);
    return;
  }
  if (request.op == OpType::Erase || request.op == OpType::Refresh) {
    const auto address = system_.mapper().map_read(first_lpn);
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
    if (request.op == OpType::Erase && system_.mapper().stripe_mapping()) {
      const auto active = system_.mapper().stripe_mapping()->active_stripe(first_lpn);
      if (!active) {
        add_subrequest(address);
      } else {
        for (std::uint32_t lane = 0;
             lane < system_.mapper().stripe_mapping()->stripe_width(); ++lane)
          add_subrequest(system_.mapper().stripe_mapping()->address_for(*active, lane));
      }
    } else {
      add_subrequest(address);
    }
    return;
  }

  const bool spec_profile =
      config_.simulation_profile != SimulationProfile::MediaResearch;
  if (spec_profile && request.op == OpType::Write) {
    const auto channel = system_.channels().translate(request.logical_addr);
    const auto transfer = reserve_host(
        request.host_route, HostLinkDirection::HostToDevice, now_,
        request.size, request.measured);
    request.dlu_data_ready = transfer.completion;
    const auto assembled = system_.dlu_assembler().submit(
        request.id, channel, request.size, now_, transfer.completion,
        transfer.start - now_, transfer.completion - transfer.start);
    if (assembled.status != HbfStatus::Pending &&
        assembled.status != HbfStatus::Success) {
      request.failed = true;
      request.status = assembled.status;
      if (request.measured) stats_.record_dlu_rejection(assembled.status);
      request.complete_time = now_;
      if (request.measured) stats_.record_request(request);
      publish_response(request);
      requests_.erase(request.id);
      return;
    }
    if (assembled.status == HbfStatus::Pending) {
      if (assembled.deadline)
        schedule(*assembled.deadline, EventType::DluTimeout, 0);
      return;
    }

    request.dlu_request_ids = assembled.completed->request_ids;
    if (request.measured)
      stats_.record_dlu_completed(assembled.completed->timing);
    auto ready = request.dlu_data_ready;
    for (const auto contributor : request.dlu_request_ids)
      ready = std::max(ready, requests_.at(contributor).dlu_data_ready);
    const auto global =
        system_.channels().global_address(assembled.completed->address);
    SubRequest sub;
    sub.id = next_subrequest_id_++;
    sub.parent_id = request.id;
    sub.op = OpType::Write;
    sub.source = TransactionSource::User;
    sub.lpn = global / config_.page_size;
    sub.bytes = assembled.completed->size;
    sub.paddr =
        system_.mapper().prepare_channel_write(assembled.completed->address);
    system_.media().invalidate_read_cache_page(sub.paddr);
    sub.arrival_time = now_;
    sub.host_route = request.host_route;
    sub.latency.host_command_wait_ns = request.host_command_wait_ns;
    sub.latency.host_command_service_ns = request.host_command_service_ns;
    sub.latency.host_data_wait_ns =
        assembled.completed->timing.total_h2d_wait_ns;
    sub.latency.host_data_service_ns =
        assembled.completed->timing.total_h2d_service_ns;
    ++request.pending_subreqs;
    subrequests_.emplace(sub.id, sub);
    schedule(ready, EventType::SubreqReady, request.id, sub.id);
    return;
  }

  if (spec_profile && request.op == OpType::Read) {
    std::uint64_t current = request.logical_addr;
    std::uint64_t remaining = request.size;
    while (remaining > 0) {
      const auto channel = system_.channels().translate(current);
      const auto channel_chunk = system_.channels().interleave() -
                                 current % system_.channels().interleave();
      const auto port_chunk = system_.channels().axi_port_interleave() -
                              channel.local_address %
                                  system_.channels().axi_port_interleave();
      const auto page_chunk = config_.page_size -
                              channel.local_address % config_.page_size;
      const auto bytes = std::min(
          {remaining, channel_chunk, port_chunk, page_chunk});
      const auto forwarding =
          system_.dlu_assembler().lookup(channel, bytes);
      SubRequest sub;
      sub.id = next_subrequest_id_++;
      sub.parent_id = request.id;
      sub.op = OpType::Read;
      sub.read_type = request.read_type;
      sub.read_retry_stage = request.retry_stage.value_or(0);
      sub.read_attempts = sub.read_retry_stage;
      sub.source = TransactionSource::User;
      sub.lpn = current / config_.page_size;
      sub.bytes = bytes;
      sub.paddr = system_.mapper().map_channel_read(channel);
      sub.arrival_time = now_;
      sub.host_route = system_.host_router().route(current, sub.paddr);
      sub.latency.host_command_wait_ns = request.host_command_wait_ns;
      sub.latency.host_command_service_ns = request.host_command_service_ns;
      ++request.pending_subreqs;
      if (forwarding.disposition == DluReadDisposition::PendingWrite) {
        if (request.measured) stats_.record_dlu_pending_read();
        sub.failed = true;
        sub.status = HbfStatus::ReadPendingWrite;
        subrequests_.emplace(sub.id, sub);
        schedule(now_, EventType::SubreqDone, request.id, sub.id);
      } else if (forwarding.disposition == DluReadDisposition::Forwarded) {
        if (request.measured) stats_.record_dlu_forwarded(bytes);
        const auto host = reserve_host(
            sub.host_route, HostLinkDirection::DeviceToHost,
            std::max(now_, forwarding.ready_at), bytes, request.measured);
        sub.latency.host_data_wait_ns = host.start - now_;
        sub.latency.host_data_service_ns = host.completion - host.start;
        subrequests_.emplace(sub.id, sub);
        schedule(host.completion, EventType::SubreqDone, request.id, sub.id);
      } else {
        if (config_.initialization_mode != InitializationMode::Empty)
          system_.media().materialize_initialized_page(sub.paddr);
        if (system_.media().read_cache_lookup(sub.paddr, now_)) {
          if (request.measured) stats_.record_read_cache_hit(bytes);
          const auto fabric = reserve_fabric(
              sub.paddr, now_, bytes, request.measured);
          sub.latency.fabric_wait_ns = fabric.start - now_;
          sub.latency.fabric_service_ns =
              fabric.completion - fabric.start;
          subrequests_.emplace(sub.id, sub);
          schedule(fabric.completion, EventType::ReadCacheDataOutDone,
                   request.id, sub.id);
        } else {
          if (request.measured) stats_.record_read_cache_miss();
          subrequests_.emplace(sub.id, sub);
          schedule(now_, EventType::SubreqReady, request.id, sub.id);
        }
      }
      current += bytes;
      remaining -= bytes;
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
                               ? system_.mapper().prepare_write(lpn)
                               : system_.mapper().map_read(lpn);
    address.offset = first_offset;
    if (request.op == OpType::Read &&
        config_.initialization_mode != InitializationMode::Empty)
      system_.media().materialize_initialized_page(address);
    SubRequest sub;
    sub.id = next_subrequest_id_++;
    sub.parent_id = request.id;
    sub.op = request.op;
    sub.read_type = request.read_type;
    sub.read_retry_stage = request.retry_stage.value_or(0);
    sub.read_attempts = sub.read_retry_stage;
    sub.source = TransactionSource::User;
    sub.lpn = lpn;
    sub.bytes = bytes;
    sub.paddr = address;
    sub.arrival_time = now_;
    sub.host_route = system_.host_router().route(lpn * config_.page_size, address);
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

bool Simulator::is_measured(std::uint64_t request_id) const {
  return requests_.at(request_id).measured;
}

void Simulator::record_queue_depth() {
  if (phase_ != SimulationPhase::Measure) return;
  std::uint64_t active = 0;
  for (const auto value : system_.controller().execution().active_per_stack)
    active += value;
  stats_.record_queue_depth(now_, queue_depth_[0], queue_depth_[1],
                            queue_depth_[2], queue_depth_[3], active);
}

void Simulator::start_array_tracking(const SubRequest& sub, SimTime now) {
  if (!is_measured(sub.parent_id)) return;
  const auto local = sub.paddr.die * config_.planes_per_die +
                     sub.paddr.plane;
  stats_.record_resource_transition(ResourceKind::Array, sub.paddr.stack,
                                    local, 1, now);
}

void Simulator::stop_array_tracking(const SubRequest& sub, SimTime now) {
  if (!is_measured(sub.parent_id)) return;
  const auto local = sub.paddr.die * config_.planes_per_die +
                     sub.paddr.plane;
  stats_.record_resource_transition(ResourceKind::Array, sub.paddr.stack,
                                    local, -1, now);
}

}  // namespace hbfsim
