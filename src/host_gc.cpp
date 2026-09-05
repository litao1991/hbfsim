#include "hbfsim/core.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace hbfsim {
namespace {

std::size_t watermark(std::size_t total, double ratio) {
  auto scaled = static_cast<long double>(total) * ratio;
  const auto nearest = std::round(scaled);
  const auto tolerance =
      4.0L * std::numeric_limits<double>::epsilon() *
      std::max(1.0L, std::abs(scaled));
  if (std::abs(scaled - nearest) <= tolerance) scaled = nearest;
  return std::min(
      total, static_cast<std::size_t>(std::ceil(scaled)));
}

struct Candidate {
  StripeId stripe;
  std::uint32_t valid = 0;
  std::uint32_t invalid = 0;
  std::uint32_t consumed = 0;
  bool erase_only = false;
};

bool better_candidate(const Candidate& candidate, const Candidate& current,
                      HostGcVictimPolicy policy) {
  if (candidate.erase_only != current.erase_only)
    return candidate.erase_only;
  if (policy == HostGcVictimPolicy::Greedy) {
    if (candidate.valid != current.valid)
      return candidate.valid < current.valid;
  } else {
    const auto candidate_score =
        static_cast<std::uint64_t>(candidate.invalid) * current.consumed;
    const auto current_score =
        static_cast<std::uint64_t>(current.invalid) * candidate.consumed;
    if (candidate_score != current_score)
      return candidate_score > current_score;
  }
  if (candidate.invalid != current.invalid)
    return candidate.invalid > current.invalid;
  return candidate.stripe.physical_id < current.stripe.physical_id;
}

}  // namespace

HostGcManager::HostGcManager(const Config& config) : config_(config) {}

HostGcPollResult HostGcManager::poll(const StripeMappingTable& mapping,
                                     bool copy_engine_busy) {
  HostGcPollResult result;
  result.free_stripes = mapping.free_stripe_count();
  result.low_watermark = watermark(mapping.total_stripe_count(),
                                   config_.host_gc_low_watermark);
  result.high_watermark = watermark(mapping.total_stripe_count(),
                                    config_.host_gc_high_watermark);
  if (result.high_watermark <= result.low_watermark &&
      result.low_watermark < mapping.total_stripe_count())
    result.high_watermark = result.low_watermark + 1;
  if (!config_.host_gc_enabled) return result;

  if (!pressure_active_) {
    if (result.free_stripes > result.low_watermark) return result;
    if (stalled_epoch_ == media_epoch_ &&
        stalled_free_stripes_ == result.free_stripes)
      return result;
    pressure_active_ = true;
    result.cycle_started = true;
  }
  if (result.free_stripes >= result.high_watermark) {
    pressure_active_ = false;
    stalled_epoch_.reset();
    stalled_free_stripes_.reset();
    result.high_watermark_reached = true;
    return result;
  }
  if (copy_engine_busy) return result;

  std::optional<Candidate> best;
  for (const auto& stripe : mapping.active_stripes()) {
    const auto& descriptor = mapping.descriptor(stripe);
    if (descriptor.state != StripeState::Sealed) continue;
    const auto invalid = descriptor.invalid_bitmap.count();
    if (invalid == 0) continue;
    Candidate candidate{stripe, descriptor.valid_slots, invalid,
                        descriptor.next_program_slot,
                        descriptor.valid_slots == 0 &&
                            !descriptor.failed_bitmap.any()};
    if (!candidate.erase_only && result.free_stripes == 0) continue;
    if (!best || better_candidate(candidate, *best,
                                  config_.host_gc_victim_policy))
      best = candidate;
  }

  if (!best) {
    pressure_active_ = false;
    stalled_epoch_ = media_epoch_;
    stalled_free_stripes_ = result.free_stripes;
    result.stalled = true;
    return result;
  }
  stalled_epoch_.reset();
  stalled_free_stripes_.reset();
  result.decision = HostGcDecision{best->stripe, best->erase_only};
  return result;
}

void Simulator::maybe_start_host_gc(SimTime now) {
  auto* mapping = mapper_.stripe_mapping();
  if (!mapping) return;
  const auto result = host_gc_manager_.poll(
      *mapping,
      active_copy_jobs(TransactionSource::GarbageCollection) != 0 ||
          active_copy_jobs(TransactionSource::Recovery) != 0 ||
          !pending_recoveries_.empty());
  const bool measured = phase_ != SimulationPhase::Warmup;
  if (measured) {
    stats_.observe_free_stripes(result.free_stripes,
                                mapping->host_visible_stripe_count());
    if (result.cycle_started) stats_.record_host_gc_cycle_started();
    if (result.high_watermark_reached)
      stats_.record_host_gc_high_watermark();
    if (result.stalled) stats_.record_host_gc_stall();
  }
  if (!result.decision) return;
  if (measured)
    stats_.record_automatic_gc_job(result.decision->erase_only);
  start_gc_job(result.decision->victim, measured, now);
}

}  // namespace hbfsim
