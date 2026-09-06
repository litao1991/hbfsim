#include "hbfsim/simulator.h"

#include <stdexcept>

namespace hbfsim {

namespace {

TransactionSource source_for(HostRewriteReason reason) {
  switch (reason) {
    case HostRewriteReason::ProgramFailure: return TransactionSource::HostReplay;
    case HostRewriteReason::Refresh: return TransactionSource::HostRefresh;
    case HostRewriteReason::WearLevel: return TransactionSource::HostWearLevel;
  }
  return TransactionSource::HostReplay;
}

}  // namespace

std::uint64_t Simulator::start_host_rewrite(std::uint64_t logical_addr) {
  return start_host_rewrite(logical_addr, HostRewriteReason::ProgramFailure);
}

std::uint64_t Simulator::start_host_refresh(std::uint64_t logical_addr) {
  return start_host_rewrite(logical_addr, HostRewriteReason::Refresh);
}

std::uint64_t Simulator::start_host_rewrite(std::uint64_t logical_addr,
                                            HostRewriteReason reason) {
  auto* mapping = system_.mapper().media_management_mapping();
  if (!mapping)
    throw std::runtime_error("HOST_REWRITE_REQUIRES_HOST_MANAGED_MAPPING");
  const auto lpn = logical_addr / config_.page_size;
  const auto stripe = mapping->active_stripe(lpn);
  if (!stripe) throw std::runtime_error("HOST_REWRITE_SOURCE_NOT_MAPPED");
  const auto& source = mapping->descriptor(*stripe);
  if (source.state != StripeState::Sealed && source.state != StripeState::Degraded)
    throw std::runtime_error("HOST_REWRITE_REQUIRES_SEALED_OR_DEGRADED_STRIPE");
  if (source.next_program_slot == 0)
    throw std::runtime_error("HOST_REWRITE_SOURCE_EMPTY");
  const auto slot = source.next_program_slot - 1;
  const auto& plan = system_.replay_manager().record(
      *stripe, slot, source.valid_slots, mapping->address_for(*stripe, slot),
      static_cast<std::uint64_t>(source.next_program_slot) * config_.page_size,
      reason);
  return start_host_replay(plan.id);
}

std::optional<RefreshDecision> Simulator::refresh_required() const {
  const auto* mapping = system_.mapper().stripe_mapping();
  if (!mapping) return std::nullopt;
  if (const auto due = system_.refresh_manager().poll(*mapping, now_, 0).decision)
    return due;
  if (config_.refresh_read_count_threshold == 0) return std::nullopt;
  for (const auto& stripe : mapping->active_stripes()) {
    const auto& descriptor = mapping->descriptor(stripe);
    if (descriptor.state != StripeState::Sealed) continue;
    for (std::uint32_t lane = 0; lane < mapping->stripe_width(); ++lane) {
      if (system_.media().block_read_count(mapping->address_for(stripe, lane)) >=
          config_.refresh_read_count_threshold)
        return RefreshDecision{stripe, now_};
    }
  }
  return std::nullopt;
}

std::optional<WearLevelPlan> Simulator::start_host_wear_leveling() {
  const auto plan = system_.wear_level_manager().decide(system_.zones());
  if (!plan) return std::nullopt;
  auto* mapping = system_.mapper().media_management_mapping();
  if (!mapping)
    throw std::runtime_error("HOST_WEAR_LEVELING_REQUIRES_HOST_MANAGED_MAPPING");
  system_.zones().remap(plan->logical_zone, plan->destination_physical_zone);
  std::vector<std::uint64_t> bases;
  for (const auto& stripe : mapping->active_stripes()) {
    const auto& descriptor = mapping->descriptor(stripe);
    if (descriptor.state == StripeState::Sealed &&
        system_.zones().logical_zone_of(descriptor.logical_base_lpn) ==
            plan->logical_zone)
      bases.push_back(descriptor.logical_base_lpn * config_.page_size);
  }
  for (const auto base : bases)
    start_host_rewrite(base, HostRewriteReason::WearLevel);
  return plan;
}

std::uint64_t Simulator::start_host_replay(std::uint64_t replay_plan_id) {
  auto* mapping = system_.mapper().media_management_mapping();
  if (!mapping)
    throw std::runtime_error("HOST_REPLAY_REQUIRES_HOST_MANAGED_MAPPING");
  const auto& plan = system_.replay_manager().plan(replay_plan_id);
  const auto& source = mapping->descriptor(plan.source_stripe);
  if (source.state != StripeState::Degraded && source.state != StripeState::Sealed)
    throw std::runtime_error("HOST_REPLAY_SOURCE_NOT_REPLAYABLE");
  if (source.next_program_slot == 0)
    throw std::runtime_error("HOST_REPLAY_SOURCE_EMPTY");

  const auto destination = mapping->allocate_replacement(source.logical_base_lpn);
  try {
    mapping->begin_migration(plan.source_stripe);
  } catch (...) {
    mapping->abort_migration(destination);
    throw;
  }
  HostReplayJob job;
  job.id = system_.replay_manager().next_job_id();
  job.plan_id = plan.id;
  job.reason = plan.reason;
  job.source = source_for(plan.reason);
  job.source_stripe = plan.source_stripe;
  job.destination_stripe = destination;
  job.slot_limit = source.next_program_slot;
  job.start_time = now_;
  const auto id = job.id;
  system_.replay_manager().jobs().emplace(id, std::move(job));
  advance_host_replay(id, now_);
  return id;
}

void Simulator::advance_host_replay(std::uint64_t job_id, SimTime now) {
  auto job_it = system_.replay_manager().jobs().find(job_id);
  if (job_it == system_.replay_manager().jobs().end()) return;
  auto& job = job_it->second;
  if (job.stage != HostReplayStage::Programming) return;
  auto* mapping = system_.mapper().media_management_mapping();
  const auto& source = mapping->descriptor(job.source_stripe);
  while (job.next_slot < job.slot_limit) {
    const auto slot = job.next_slot++;
    if (source.hole_bitmap.test(slot) || source.invalid_bitmap.test(slot)) {
      const auto lpn = source.logical_base_lpn + slot;
      mapping->reserve_hole(job.destination_stripe, lpn);
      system_.media().reserve_program_hole(
          mapping->address_for(job.destination_stripe, slot));
      continue;
    }
    enqueue_host_replay_program(job_id, slot, now);
    return;  // strictly sequential host payload replay
  }
  auto& destination = mapping->descriptor(job.destination_stripe);
  if (destination.state == StripeState::Open) mapping->seal(job.destination_stripe);
  mapping->remap_commit(job.source_stripe, job.destination_stripe);
  if (job.measured)
    stats_.record_remap_commit(job.source,
                               now - job.start_time);
  enqueue_host_replay_erases(job_id, now);
}

void Simulator::enqueue_host_replay_program(std::uint64_t job_id,
                                            std::uint32_t slot, SimTime now) {
  auto& job = system_.replay_manager().jobs().at(job_id);
  auto* mapping = system_.mapper().media_management_mapping();
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
  request.host_route = system_.host_router().route(request.logical_addr, address);
  const auto command = reserve_host(request.host_route, HostLinkDirection::Command,
                                    now, 64, request.measured);
  const auto data = reserve_host(request.host_route, HostLinkDirection::HostToDevice,
                                 command.completion, config_.page_size,
                                 request.measured);
  request.host_command_wait_ns = command.start - now;
  request.host_command_service_ns = command.completion - command.start;

  SubRequest sub;
  sub.id = next_subrequest_id_++;
  sub.parent_id = request.id;
  sub.op = OpType::Write;
  sub.source = job.source;
  sub.lpn = lpn;
  sub.bytes = config_.page_size;
  sub.paddr = address;
  sub.arrival_time = now;
  sub.host_route = request.host_route;
  sub.host_replay_job_id = job_id;
  sub.copy_slot = slot;
  sub.critical = true;
  sub.latency.host_command_wait_ns = request.host_command_wait_ns;
  sub.latency.host_command_service_ns = request.host_command_service_ns;
  sub.latency.host_data_wait_ns = data.start - command.completion;
  sub.latency.host_data_service_ns = data.completion - data.start;
  ++request.pending_subreqs;
  const auto request_id = request.id;
  const auto subrequest_id = sub.id;
  requests_.emplace(request_id, std::move(request));
  subrequests_.emplace(subrequest_id, std::move(sub));
  schedule(data.completion, EventType::SubreqReady, request_id, subrequest_id);
}

void Simulator::enqueue_host_replay_erases(std::uint64_t job_id, SimTime now) {
  auto& job = system_.replay_manager().jobs().at(job_id);
  auto* mapping = system_.mapper().media_management_mapping();
  job.stage = HostReplayStage::ErasingSource;
  job.pending_erases = mapping->stripe_width();
  enqueue_stripe_erases(job.source_stripe, job.source,
                        job.measured, std::nullopt, now, job_id);
}

void Simulator::handle_host_replay_completion(std::uint64_t job_id,
                                              std::uint32_t slot, OpType op,
                                              bool failed, SimTime now) {
  auto job_it = system_.replay_manager().jobs().find(job_id);
  if (job_it == system_.replay_manager().jobs().end()) return;
  auto& job = job_it->second;
  auto* mapping = system_.mapper().media_management_mapping();
  if (job.stage == HostReplayStage::ErasingSource ||
      job.stage == HostReplayStage::CleaningDestination) {
    if (op != OpType::Erase) throw std::logic_error("HOST_REPLAY_ERASE_INVARIANT");
    if (failed || job.pending_erases == 0) {
      job.stage = HostReplayStage::Failed;
      if (job.measured)
        stats_.record_host_rewrite_job(job.source, true, now - job.start_time);
      system_.replay_manager().jobs().erase(job_it);
      return;
    }
    if (--job.pending_erases != 0) return;
    if (job.measured)
      stats_.record_host_rewrite_job(
          job.source, job.stage == HostReplayStage::CleaningDestination,
          now - job.start_time);
    job.stage = HostReplayStage::Complete;
    system_.replay_manager().jobs().erase(job_it);
    return;
  }
  if (op != OpType::Write || slot >= job.slot_limit)
    throw std::logic_error("HOST_REPLAY_PROGRAM_INVARIANT");
  if (failed) {
    // A second Program Failure has already produced a new host-facing plan.
    // Restore the old mapping, clean the abandoned destination, and let the
    // Host choose when to execute the new plan.
    mapping->abort_migration(job.destination_stripe);
    if (job.measured) stats_.record_aborted_migration();
    job.stage = HostReplayStage::CleaningDestination;
    job.pending_erases = mapping->stripe_width();
    enqueue_stripe_erases(job.destination_stripe, job.source, job.measured,
                          std::nullopt, now, job_id);
    return;
  }
  advance_host_replay(job_id, now);
}

}  // namespace hbfsim
