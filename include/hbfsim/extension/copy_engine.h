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

}  // namespace hbfsim
