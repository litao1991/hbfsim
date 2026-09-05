#include "hbfsim/core.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace hbfsim {
namespace {

bool copy_source(TransactionSource source) {
  return source == TransactionSource::Recovery ||
         source == TransactionSource::GarbageCollection;
}

}  // namespace

std::uint64_t Simulator::start_host_gc(std::uint64_t logical_addr) {
  auto* mapping = mapper_.stripe_mapping();
  if (!mapping)
    throw std::runtime_error("HOST_GC_REQUIRES_HOST_MANAGED_MAPPING");
  const auto lpn = logical_addr / config_.page_size;
  const auto source = mapping->active_stripe(lpn);
  if (!source) throw std::runtime_error("HOST_GC_SOURCE_NOT_MAPPED");
  return start_gc_job(*source, true, now_);
}

std::uint64_t Simulator::start_gc_job(const StripeId& stripe, bool measured,
                                      SimTime now) {
  auto* mapping = mapper_.stripe_mapping();
  const auto& descriptor = mapping->descriptor(stripe);
  if (descriptor.state == StripeState::Open) mapping->seal(stripe);
  const auto& sealed = mapping->descriptor(stripe);
  if (sealed.state != StripeState::Sealed)
    throw std::runtime_error("HOST_GC_REQUIRES_SEALED_STRIPE");
  if (sealed.valid_slots == 0 && !sealed.failed_bitmap.any())
    return start_gc_erase_only(stripe, measured, now);
  return start_copy_job(TransactionSource::GarbageCollection, stripe,
                        std::nullopt, measured, now);
}

std::uint64_t Simulator::start_gc_erase_only(
    const StripeId& stripe, bool measured, SimTime now) {
  auto* mapping = mapper_.stripe_mapping();
  mapping->begin_migration(stripe);
  CopyJob job;
  job.id = next_copy_job_id_++;
  job.source = TransactionSource::GarbageCollection;
  job.source_stripe = stripe;
  job.slot_limit = mapping->descriptor(stripe).next_program_slot;
  job.pending_erases = mapping->stripe_width();
  job.start_time = now;
  job.measured = measured;
  job.erase_only = true;
  job.stage = CopyStage::ErasingSource;
  const auto id = job.id;
  copy_jobs_.emplace(id, std::move(job));
  enqueue_stripe_erases(stripe, TransactionSource::GarbageCollection,
                        measured, id, now);
  return id;
}

std::uint64_t Simulator::start_copy_job(
    TransactionSource source, const StripeId& stripe,
    std::optional<std::uint32_t> replay_slot, bool measured, SimTime now) {
  if (!copy_source(source))
    throw std::invalid_argument("copy job requires recovery or GC source");
  auto* mapping = mapper_.stripe_mapping();
  if (!mapping)
    throw std::runtime_error("COPY_REQUIRES_HOST_MANAGED_MAPPING");

  const auto& source_descriptor = mapping->descriptor(stripe);
  const auto slot_limit = source_descriptor.next_program_slot;
  const auto logical_base = source_descriptor.logical_base_lpn;
  const auto destination = mapping->allocate_replacement(logical_base);
  try {
    mapping->begin_migration(stripe);
  } catch (...) {
    mapping->abort_migration(destination);
    throw;
  }

  CopyJob job;
  job.id = next_copy_job_id_++;
  job.source = source;
  job.source_stripe = stripe;
  job.destination_stripe = destination;
  job.slot_limit = slot_limit;
  job.replay_slot = replay_slot;
  job.attempts = 1;
  job.start_time = now;
  job.measured = measured;
  reset_copy_attempt(job);
  const auto id = job.id;
  copy_jobs_.emplace(id, std::move(job));
  advance_copy_job(id, now);
  return id;
}

void Simulator::enqueue_copy_read(std::uint64_t job_id, std::uint32_t slot,
                                  SimTime now) {
  auto& job = copy_jobs_.at(job_id);
  auto* mapping = mapper_.stripe_mapping();
  const auto& source = mapping->descriptor(job.source_stripe);
  const auto lpn = source.logical_base_lpn + slot;
  const auto address = mapping->address_for(job.source_stripe, slot);

  Request request;
  request.id = next_request_id_++;
  request.arrival_time = now;
  request.op = OpType::Read;
  request.logical_addr = lpn * config_.page_size;
  request.size = config_.page_size;
  request.measured = job.measured;
  request.internal = true;
  request.source = job.source;
  request.host_route = host_router_.route(request.logical_addr, address);
  const auto command = reserve_host(request.host_route,
                                    HostLinkDirection::Command, now, 64,
                                    request.measured);
  request.host_command_wait_ns = command.start - now;
  request.host_command_service_ns = command.completion - command.start;

  SubRequest subrequest;
  subrequest.id = next_subrequest_id_++;
  subrequest.parent_id = request.id;
  subrequest.op = OpType::Read;
  subrequest.source = job.source;
  subrequest.lpn = lpn;
  subrequest.bytes = config_.page_size;
  subrequest.paddr = address;
  subrequest.arrival_time = now;
  subrequest.host_route = request.host_route;
  subrequest.copy_job_id = job_id;
  subrequest.copy_slot = slot;
  subrequest.critical = job.source == TransactionSource::Recovery;
  subrequest.latency.host_command_wait_ns = request.host_command_wait_ns;
  subrequest.latency.host_command_service_ns =
      request.host_command_service_ns;
  ++request.pending_subreqs;
  const auto request_id = request.id;
  const auto subrequest_id = subrequest.id;
  requests_.emplace(request_id, std::move(request));
  subrequests_.emplace(subrequest_id, std::move(subrequest));
  schedule(command.completion, EventType::SubreqReady, request_id,
           subrequest_id);
}

void Simulator::enqueue_copy_program(std::uint64_t job_id,
                                     std::uint32_t slot, SimTime now) {
  auto& job = copy_jobs_.at(job_id);
  auto* mapping = mapper_.stripe_mapping();
  const auto& destination = mapping->descriptor(job.destination_stripe);
  const auto lpn = destination.logical_base_lpn + slot;
  const auto address = mapping->reserve_program(job.destination_stripe, lpn);

  Request request;
  request.id = next_request_id_++;
  request.arrival_time = now;
  request.op = OpType::Write;
  request.logical_addr = lpn * config_.page_size;
  request.size = config_.page_size;
  request.measured = job.measured;
  request.internal = true;
  request.source = job.source;
  request.host_route = host_router_.route(request.logical_addr, address);
  const auto command = reserve_host(request.host_route,
                                    HostLinkDirection::Command, now, 64,
                                    request.measured);
  request.host_command_wait_ns = command.start - now;
  request.host_command_service_ns = command.completion - command.start;
  const auto data = reserve_host(request.host_route,
                                 HostLinkDirection::HostToDevice,
                                 command.completion, config_.page_size,
                                 request.measured);

  SubRequest subrequest;
  subrequest.id = next_subrequest_id_++;
  subrequest.parent_id = request.id;
  subrequest.op = OpType::Write;
  subrequest.source = job.source;
  subrequest.lpn = lpn;
  subrequest.bytes = config_.page_size;
  subrequest.paddr = address;
  subrequest.arrival_time = now;
  subrequest.host_route = request.host_route;
  subrequest.copy_job_id = job_id;
  subrequest.copy_slot = slot;
  subrequest.critical = job.source == TransactionSource::Recovery;
  subrequest.latency.host_command_wait_ns = request.host_command_wait_ns;
  subrequest.latency.host_command_service_ns =
      request.host_command_service_ns;
  subrequest.latency.host_data_wait_ns = data.start - command.completion;
  subrequest.latency.host_data_service_ns = data.completion - data.start;
  ++request.pending_subreqs;
  const auto request_id = request.id;
  const auto subrequest_id = subrequest.id;
  requests_.emplace(request_id, std::move(request));
  subrequests_.emplace(subrequest_id, std::move(subrequest));
  schedule(data.completion, EventType::SubreqReady, request_id,
           subrequest_id);
}

void Simulator::reserve_copy_hole(CopyJob& job, std::uint32_t slot) {
  auto* mapping = mapper_.stripe_mapping();
  const auto& source = mapping->descriptor(job.source_stripe);
  mapping->reserve_hole(job.destination_stripe,
                        source.logical_base_lpn + slot);
  const auto address = mapping->address_for(job.destination_stripe, slot);
  auto& block = plane(address).blocks.at(address.block);
  if (block.next_program_page != address.page)
    throw std::logic_error("copy hole violates block program order");
  if (block.state == BlockState::Free) block.state = BlockState::Open;
  ++block.next_program_page;
  if (block.next_program_page == config_.pages_per_block)
    block.state = BlockState::Closed;
}

void Simulator::enqueue_copy_erases(std::uint64_t job_id, SimTime now) {
  auto& job = copy_jobs_.at(job_id);
  auto* mapping = mapper_.stripe_mapping();
  job.stage = CopyStage::ErasingSource;
  job.pending_erases = mapping->stripe_width();
  enqueue_stripe_erases(job.source_stripe, job.source, job.measured,
                        job_id, now);
}

void Simulator::enqueue_stripe_erases(
    const StripeId& stripe, TransactionSource source, bool measured,
    std::optional<std::uint64_t> copy_job_id, SimTime now) {
  auto* mapping = mapper_.stripe_mapping();
  std::vector<PhysicalAddr> blocks;
  blocks.reserve(mapping->stripe_width());
  for (std::uint32_t lane = 0; lane < mapping->stripe_width(); ++lane)
    blocks.push_back(mapping->address_for(stripe, lane));

  for (const auto& address : blocks) {
    Request request;
    request.id = next_request_id_++;
    request.arrival_time = now;
    request.op = OpType::Erase;
    request.measured = measured;
    request.internal = true;
    request.source = source;
    request.host_route = host_router_.route(0, address);
    const auto command = reserve_host(request.host_route,
                                      HostLinkDirection::Command, now, 64,
                                      measured);
    request.host_command_wait_ns = command.start - now;
    request.host_command_service_ns = command.completion - command.start;

    SubRequest subrequest;
    subrequest.id = next_subrequest_id_++;
    subrequest.parent_id = request.id;
    subrequest.op = OpType::Erase;
    subrequest.source = source;
    subrequest.paddr = address;
    subrequest.arrival_time = now;
    subrequest.host_route = request.host_route;
    subrequest.copy_job_id = copy_job_id;
    subrequest.latency.host_command_wait_ns = request.host_command_wait_ns;
    subrequest.latency.host_command_service_ns =
        request.host_command_service_ns;
    ++request.pending_subreqs;
    const auto request_id = request.id;
    const auto subrequest_id = subrequest.id;
    requests_.emplace(request_id, std::move(request));
    subrequests_.emplace(subrequest_id, std::move(subrequest));
    schedule(command.completion, EventType::SubreqReady, request_id,
             subrequest_id);
  }
}

void Simulator::reset_copy_attempt(CopyJob& job) {
  job.next_read_slot = 0;
  job.next_program_slot = 0;
  job.inflight_reads = 0;
  job.inflight_programs = 0;
  job.buffer_reserved_bytes = 0;
  job.buffer_used_bytes = 0;
  job.failure_pending = false;
  job.retry_after_drain = false;
  job.slots.clear();
}

void Simulator::advance_copy_job(std::uint64_t job_id, SimTime now) {
  auto& job = copy_jobs_.at(job_id);
  if (job.stage != CopyStage::Copying) return;
  if (job.failure_pending) {
    handle_copy_failure_drain(job_id, now);
    return;
  }

  auto* mapping = mapper_.stripe_mapping();
  const auto& source = mapping->descriptor(job.source_stripe);
  bool progress = true;
  while (progress && !job.failure_pending) {
    progress = false;
    const auto window_end = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        job.slot_limit,
        static_cast<std::uint64_t>(job.next_program_slot) +
            config_.copy_prefetch_window_pages));
    while (job.next_read_slot < window_end &&
           job.inflight_reads < config_.copy_max_inflight_reads) {
      const auto slot = job.next_read_slot;
      if (!source.valid_bitmap.test(slot)) {
        ++job.next_read_slot;
        progress = true;
        continue;
      }
      const auto occupied = job.buffer_used_bytes +
                            job.buffer_reserved_bytes;
      if (config_.copy_buffer_size - occupied < config_.page_size) break;
      job.slots.emplace(slot, CopySlotStage::Reading);
      ++job.inflight_reads;
      job.buffer_reserved_bytes += config_.page_size;
      ++job.next_read_slot;
      enqueue_copy_read(job_id, slot, now);
      progress = true;
    }

    while (job.next_program_slot < job.slot_limit &&
           job.inflight_programs < config_.copy_max_inflight_programs) {
      const auto slot = job.next_program_slot;
      if (source.failed_bitmap.test(slot)) {
        if (job.source != TransactionSource::Recovery ||
            (job.replay_slot && *job.replay_slot != slot)) {
          job.failure_pending = true;
          job.retry_after_drain = false;
          break;
        }
        job.slots[slot] = CopySlotStage::ProgrammingReplay;
        ++job.inflight_programs;
        ++job.next_program_slot;
        enqueue_copy_program(job_id, slot, now);
        progress = true;
        continue;
      }
      if (!source.valid_bitmap.test(slot)) {
        reserve_copy_hole(job, slot);
        ++job.next_program_slot;
        progress = true;
        continue;
      }
      const auto state = job.slots.find(slot);
      if (state == job.slots.end() ||
          state->second == CopySlotStage::Reading)
        break;
      if (state->second != CopySlotStage::Buffered)
        throw std::logic_error("copy slot is not ready for program");
      state->second = CopySlotStage::ProgrammingBuffered;
      ++job.inflight_programs;
      ++job.next_program_slot;
      enqueue_copy_program(job_id, slot, now);
      progress = true;
    }
  }

  if (job.failure_pending) {
    handle_copy_failure_drain(job_id, now);
    return;
  }
  if (job.next_program_slot != job.slot_limit || job.inflight_reads != 0 ||
      job.inflight_programs != 0)
    return;
  if (job.buffer_reserved_bytes != 0 || job.buffer_used_bytes != 0 ||
      !job.slots.empty())
    throw std::logic_error("copy pipeline drained with buffered data");

  auto& destination = mapping->descriptor(job.destination_stripe);
  if (destination.state == StripeState::Open)
    mapping->seal(job.destination_stripe);
  mapping->remap_commit(job.source_stripe, job.destination_stripe);
  if (job.measured)
    stats_.record_remap_commit(job.source, now - job.start_time);
  enqueue_copy_erases(job_id, now);
}

void Simulator::handle_copy_completion(
    std::uint64_t job_id, std::optional<std::uint32_t> slot, OpType op,
    bool failed, SimTime now) {
  const auto job_it = copy_jobs_.find(job_id);
  if (job_it == copy_jobs_.end()) return;
  auto& job = job_it->second;
  if (job.stage == CopyStage::ErasingSource) {
    if (op != OpType::Erase)
      throw std::logic_error("copy job erase-stage invariant violated");
    if (failed) {
      finish_copy_job(job_id, now, true);
      return;
    }
    if (job.pending_erases == 0)
      throw std::logic_error("copy job erase count underflow");
    --job.pending_erases;
    if (job.pending_erases == 0) finish_copy_job(job_id, now, false);
    return;
  }
  if (job.stage == CopyStage::CleaningDestination) {
    if (op != OpType::Erase || failed)
      throw std::logic_error(
          "copy job destination-cleanup invariant violated");
    if (job.pending_erases == 0)
      throw std::logic_error("copy job cleanup count underflow");
    --job.pending_erases;
    if (job.pending_erases != 0) return;
    if (job.attempts >= config_.max_recovery_attempts) {
      finish_copy_job(job_id, now, true);
      return;
    }
    auto* mapping = mapper_.stripe_mapping();
    job.destination_stripe = mapping->allocate_replacement(
        mapping->descriptor(job.source_stripe).logical_base_lpn);
    mapping->begin_migration(job.source_stripe);
    ++job.attempts;
    job.stage = CopyStage::Copying;
    reset_copy_attempt(job);
    advance_copy_job(job_id, now);
    return;
  }
  if (!slot || (op != OpType::Read && op != OpType::Write))
    throw std::logic_error("copy data completion is missing its slot");
  const auto slot_it = job.slots.find(*slot);
  if (slot_it == job.slots.end())
    throw std::logic_error("unknown copy slot completion");
  if (op == OpType::Read) {
    if (slot_it->second != CopySlotStage::Reading ||
        job.inflight_reads == 0 ||
        job.buffer_reserved_bytes < config_.page_size)
      throw std::logic_error("copy read accounting invariant violated");
    --job.inflight_reads;
    job.buffer_reserved_bytes -= config_.page_size;
    if (failed) {
      job.slots.erase(slot_it);
    } else {
      slot_it->second = CopySlotStage::Buffered;
      job.buffer_used_bytes += config_.page_size;
      job.buffer_high_watermark =
          std::max(job.buffer_high_watermark, job.buffer_used_bytes);
    }
  } else {
    const bool buffered =
        slot_it->second == CopySlotStage::ProgrammingBuffered;
    if (!buffered &&
        slot_it->second != CopySlotStage::ProgrammingReplay)
      throw std::logic_error("copy program accounting invariant violated");
    if (job.inflight_programs == 0 ||
        (buffered && job.buffer_used_bytes < config_.page_size))
      throw std::logic_error("copy program count underflow");
    --job.inflight_programs;
    if (buffered) job.buffer_used_bytes -= config_.page_size;
    job.slots.erase(slot_it);
  }

  if (failed) {
    if (!job.failure_pending)
      job.retry_after_drain = op == OpType::Write;
    else if (op == OpType::Read)
      job.retry_after_drain = false;
    job.failure_pending = true;
  }
  if (job.failure_pending) {
    handle_copy_failure_drain(job_id, now);
    return;
  }
  advance_copy_job(job_id, now);
}

void Simulator::handle_copy_failure_drain(std::uint64_t job_id,
                                          SimTime now) {
  auto& job = copy_jobs_.at(job_id);
  if (job.inflight_reads != 0 || job.inflight_programs != 0) return;
  const bool retry = job.retry_after_drain;
  if (retry)
    restart_copy_job(job_id, now);
  else
    finish_copy_job(job_id, now, true);
}

void Simulator::restart_copy_job(std::uint64_t job_id, SimTime now) {
  auto& job = copy_jobs_.at(job_id);
  auto* mapping = mapper_.stripe_mapping();
  const auto abandoned = job.destination_stripe;
  mapping->abort_migration(job.destination_stripe);
  if (job.measured) stats_.record_aborted_migration();
  job.stage = CopyStage::CleaningDestination;
  job.pending_erases = mapping->stripe_width();
  enqueue_stripe_erases(abandoned, job.source, job.measured, job_id, now);
}

void Simulator::finish_copy_job(std::uint64_t job_id, SimTime now,
                                bool failed) {
  const auto it = copy_jobs_.find(job_id);
  if (it == copy_jobs_.end()) return;
  if (failed && !it->second.erase_only) {
    auto* mapping = mapper_.stripe_mapping();
    const auto state = mapping->descriptor(it->second.destination_stripe).state;
    if (state != StripeState::Stale && state != StripeState::Free) {
      const auto abandoned = it->second.destination_stripe;
      mapping->abort_migration(it->second.destination_stripe);
      if (it->second.measured) stats_.record_aborted_migration();
      enqueue_stripe_erases(abandoned, it->second.source,
                            it->second.measured, std::nullopt, now);
    }
  }
  if (it->second.measured)
    stats_.record_copy_buffer_high_watermark(
        it->second.source, it->second.buffer_high_watermark);
  if (it->second.measured)
    stats_.record_copy_job(it->second.source, failed);
  copy_jobs_.erase(it);
}

void Simulator::start_ready_recoveries(SimTime now) {
  auto* mapping = mapper_.stripe_mapping();
  if (!mapping) return;
  for (auto it = pending_recoveries_.begin();
       it != pending_recoveries_.end();) {
    const auto& descriptor = mapping->descriptor(it->source_stripe);
    if (descriptor.state != StripeState::Degraded ||
        descriptor.reserved_programs != 0) {
      ++it;
      continue;
    }
    const auto source = it->source_stripe;
    const auto measured = it->measured;
    it = pending_recoveries_.erase(it);
    if (mapping->free_stripe_count() == 0) {
      if (measured)
        stats_.record_copy_job(TransactionSource::Recovery, true);
      continue;
    }
    start_copy_job(TransactionSource::Recovery, source, std::nullopt,
                   measured, now);
  }
}

}  // namespace hbfsim
