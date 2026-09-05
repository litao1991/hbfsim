#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/mapping/stripe_mapping.h"
#include <cstddef>
#include <optional>

namespace hbfsim {

struct RefreshDecision {
  StripeId source;
  SimTime deadline = 0;
};

struct RefreshPollResult {
  std::optional<RefreshDecision> decision;
  std::optional<SimTime> next_check_at;
  bool deadline_missed = false;
  bool deferred_no_space = false;
};

class RefreshManager {
 public:
  explicit RefreshManager(const Config& config) : config_(config) {}
  RefreshPollResult poll(const StripeMappingTable& mapping, SimTime now,
                         std::size_t active_refresh_jobs) const;

 private:
  const Config& config_;
};

}  // namespace hbfsim
