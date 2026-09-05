#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/mapping/stripe_mapping.h"
#include <cstdint>
#include <cstddef>
#include <optional>

namespace hbfsim {

struct HostGcDecision {
  StripeId victim;
  bool erase_only = false;
};

struct HostGcPollResult {
  std::optional<HostGcDecision> decision;
  std::size_t free_stripes = 0;
  std::size_t low_watermark = 0;
  std::size_t high_watermark = 0;
  bool cycle_started = false;
  bool high_watermark_reached = false;
  bool stalled = false;
};

class HostGcManager {
 public:
  explicit HostGcManager(const Config& config);
  HostGcPollResult poll(const StripeMappingTable& mapping,
                        bool copy_engine_busy);
  void notify_media_change() { ++media_epoch_; }
  bool pressure_active() const { return pressure_active_; }

 private:
  const Config& config_;
  bool pressure_active_ = false;
  std::uint64_t media_epoch_ = 0;
  std::optional<std::uint64_t> stalled_epoch_;
  std::optional<std::size_t> stalled_free_stripes_;
};

}  // namespace hbfsim
