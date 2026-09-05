#include "hbfsim/core.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hbfsim {
namespace {

SimTime saturating_add(SimTime left, SimTime right) {
  if (right > std::numeric_limits<SimTime>::max() - left)
    return std::numeric_limits<SimTime>::max();
  return left + right;
}

}  // namespace

RefreshPollResult RefreshManager::poll(
    const StripeMappingTable& mapping, SimTime now,
    std::size_t active_refresh_jobs) const {
  RefreshPollResult result;
  if (!config_.automatic_refresh_enabled) return result;

  std::optional<RefreshDecision> due;
  for (const auto& stripe : mapping.active_stripes()) {
    const auto& descriptor = mapping.descriptor(stripe);
    if (descriptor.state != StripeState::Sealed ||
        descriptor.valid_slots == 0)
      continue;
    const auto deadline = saturating_add(descriptor.retention_since,
                                         config_.retention_time_ns);
    const auto trigger = deadline > config_.refresh_guard_time_ns
                             ? deadline - config_.refresh_guard_time_ns
                             : 0;
    if (trigger > now) {
      if (!result.next_check_at || trigger < *result.next_check_at)
        result.next_check_at = trigger;
      continue;
    }
    if (!due || deadline < due->deadline ||
        (deadline == due->deadline &&
         stripe.physical_id < due->source.physical_id))
      due = RefreshDecision{stripe, deadline};
  }

  if (!due ||
      active_refresh_jobs >= config_.max_concurrent_refresh_jobs)
    return result;
  result.deadline_missed = now > due->deadline;
  if (mapping.free_stripe_count() == 0) {
    result.deferred_no_space = true;
    return result;
  }
  result.decision = due;
  return result;
}

std::size_t Simulator::active_copy_jobs(TransactionSource source) const {
  return static_cast<std::size_t>(std::count_if(
      copy_jobs_.begin(), copy_jobs_.end(), [&](const auto& entry) {
        return entry.second.source == source;
      }));
}

std::uint64_t Simulator::start_refresh(std::uint64_t logical_addr) {
  auto* mapping = mapper_.stripe_mapping();
  if (!mapping)
    throw std::runtime_error("REFRESH_REQUIRES_HOST_MANAGED_MAPPING");
  const auto lpn = logical_addr / config_.page_size;
  const auto source = mapping->active_stripe(lpn);
  if (!source) throw std::runtime_error("REFRESH_SOURCE_NOT_MAPPED");
  if (mapping->descriptor(*source).state == StripeState::Open)
    mapping->seal(*source);
  if (mapping->descriptor(*source).state != StripeState::Sealed)
    throw std::runtime_error("REFRESH_REQUIRES_SEALED_STRIPE");
  return start_copy_job(TransactionSource::Refresh, *source, std::nullopt,
                        true, now_);
}

bool Simulator::has_refresh_horizon(SimTime when) const {
  if (next_trace_arrival_ && when <= *next_trace_arrival_) return true;
  return std::any_of(requests_.begin(), requests_.end(),
                     [&](const auto& entry) {
                       const auto& request = entry.second;
                       return !request.internal && request.arrival_time > now_ &&
                              when <= request.arrival_time;
                     });
}

void Simulator::schedule_refresh_check(SimTime when) {
  if (!config_.automatic_refresh_enabled || when <= now_ ||
      when >= refresh_check_at_ || !has_refresh_horizon(when))
    return;
  refresh_check_at_ = when;
  schedule(when, EventType::RefreshManagerWake, 0);
}

void Simulator::maybe_start_automatic_refresh(SimTime now) {
  auto* mapping = mapper_.stripe_mapping();
  if (!mapping || !config_.automatic_refresh_enabled) return;
  const auto result = refresh_manager_.poll(
      *mapping, now, active_copy_jobs(TransactionSource::Refresh));
  const bool measured = phase_ != SimulationPhase::Warmup;
  if (measured && result.deferred_no_space)
    stats_.record_refresh_deferred();
  if (result.decision) {
    if (measured)
      stats_.record_automatic_refresh_job(result.deadline_missed);
    start_copy_job(TransactionSource::Refresh, result.decision->source,
                   std::nullopt, measured, now);
  }
  if (result.next_check_at) schedule_refresh_check(*result.next_check_at);
}

}  // namespace hbfsim
