#pragma once

#include "hbfsim/common/types.h"
#include "hbfsim/mapping/stripe_mapping.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace hbfsim {

enum class CopyStage { Copying, ErasingSource, CleaningDestination };
enum class CopySlotStage {
  Reading,
  Buffered,
  ProgrammingBuffered,
  ProgrammingReplay,
};

struct CopyJob {
  std::uint64_t id = 0;
  TransactionSource source = TransactionSource::Recovery;
  StripeId source_stripe;
  StripeId destination_stripe;
  std::uint32_t next_read_slot = 0;
  std::uint32_t next_program_slot = 0;
  std::uint32_t slot_limit = 0;
  std::optional<std::uint32_t> replay_slot;
  std::uint32_t attempts = 0;
  std::uint32_t pending_erases = 0;
  std::uint32_t inflight_reads = 0;
  std::uint32_t inflight_programs = 0;
  std::uint64_t buffer_reserved_bytes = 0;
  std::uint64_t buffer_used_bytes = 0;
  std::uint64_t buffer_high_watermark = 0;
  SimTime start_time = 0;
  bool measured = true;
  bool failure_pending = false;
  bool retry_after_drain = false;
  bool erase_only = false;
  CopyStage stage = CopyStage::Copying;
  std::unordered_map<std::uint32_t, CopySlotStage> slots;
};

struct PendingRecovery {
  StripeId source_stripe;
  bool measured = true;
};

// Host-facing recovery contract. The device records failed placement and the
// live prefix; Host software owns payload reconstruction and replay timing.
struct ReplayPlan {
  std::uint64_t id = 0;
  StripeId source_stripe;
  std::uint32_t failed_slot = 0;
  std::uint32_t committed_slots = 0;
  PhysicalAddr failed_ppa;
  std::uint64_t replay_bytes = 0;
  enum class Reason { ProgramFailure, Refresh, WearLevel };
  Reason reason = Reason::ProgramFailure;
};

using HostRewriteReason = ReplayPlan::Reason;

enum class HostRewriteStage {
  Programming,
  ErasingSource,
  CleaningDestination,
  Complete,
  Failed,
};

// This engine deliberately models host supplied payloads.  It contains no
// device-side read/buffer path, unlike CopyEngine.
struct HostRewriteJob {
  std::uint64_t id = 0;
  std::uint64_t plan_id = 0;
  TransactionSource source = TransactionSource::HostReplay;
  HostRewriteReason reason = HostRewriteReason::ProgramFailure;
  StripeId source_stripe;
  StripeId destination_stripe;
  std::uint32_t next_slot = 0;
  std::uint32_t slot_limit = 0;
  std::uint32_t pending_erases = 0;
  SimTime start_time = 0;
  bool measured = true;
  HostRewriteStage stage = HostRewriteStage::Programming;
};

using HostReplayStage = HostRewriteStage;
using HostReplayJob = HostRewriteJob;

class CopyEngine {
 public:
  std::uint64_t next_job_id() { return next_job_id_++; }
  std::size_t size() const { return jobs_.size(); }
  std::size_t active_jobs(TransactionSource source) const;

  std::unordered_map<std::uint64_t, CopyJob>& jobs() { return jobs_; }
  const std::unordered_map<std::uint64_t, CopyJob>& jobs() const {
    return jobs_;
  }
  std::vector<PendingRecovery>& pending_recoveries() {
    return pending_recoveries_;
  }
  const std::vector<PendingRecovery>& pending_recoveries() const {
    return pending_recoveries_;
  }

 private:
  std::unordered_map<std::uint64_t, CopyJob> jobs_;
  std::vector<PendingRecovery> pending_recoveries_;
  std::uint64_t next_job_id_ = 0;
};

// Owns Host-payload rewrite plans/jobs.  Simulator drives DES events, while
// this component owns rewrite reason, identity, and lifecycle state.
class HostRewriteEngine {
 public:
  const ReplayPlan& record(const ProgramFailureNotice& notice,
                           std::uint64_t page_size) {
    return record(notice.stripe, notice.failed_slot, notice.committed_slots,
                  notice.failed_ppa,
                  static_cast<std::uint64_t>(notice.committed_slots + 1) *
                      page_size,
                  HostRewriteReason::ProgramFailure);
    return plans_.back();
  }
  const std::vector<ReplayPlan>& plans() const { return plans_; }
  const ReplayPlan& plan(std::uint64_t id) const {
    if (id >= plans_.size()) throw std::out_of_range("UNKNOWN_REPLAY_PLAN");
    return plans_.at(id);
  }
  const ReplayPlan& record(const StripeId& stripe, std::uint32_t failed_slot,
                           std::uint32_t committed_slots,
                           const PhysicalAddr& failed_ppa,
                           std::uint64_t replay_bytes,
                           HostRewriteReason reason =
                               HostRewriteReason::ProgramFailure) {
    plans_.push_back({next_id_++, stripe, failed_slot, committed_slots,
                      failed_ppa, replay_bytes, reason});
    return plans_.back();
  }
  std::uint64_t next_job_id() { return next_job_id_++; }
  std::unordered_map<std::uint64_t, HostRewriteJob>& jobs() { return jobs_; }
  const std::unordered_map<std::uint64_t, HostRewriteJob>& jobs() const {
    return jobs_;
  }

 private:
  std::vector<ReplayPlan> plans_;
  std::uint64_t next_id_ = 0;
  std::unordered_map<std::uint64_t, HostRewriteJob> jobs_;
  std::uint64_t next_job_id_ = 0;
};

// Compatibility alias for consumers of the v0.6 public name.
using HostReplayManager = HostRewriteEngine;

}  // namespace hbfsim
