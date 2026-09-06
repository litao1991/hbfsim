#pragma once

#include "hbfsim/management/zones.h"

#include <optional>

namespace hbfsim {

struct WearLevelPlan {
  std::uint32_t logical_zone = 0;
  std::uint32_t source_physical_zone = 0;
  std::uint32_t destination_physical_zone = 0;
  std::uint32_t source_max_pec = 0;
  double destination_avg_pec = 0.0;
};

// Policy only chooses a move. Simulator executes the resulting Host rewrites,
// keeping payload ownership explicitly on the Host side.
class HostWearLevelManager {
 public:
  explicit HostWearLevelManager(const Config& config) : config_(config) {}
  std::optional<WearLevelPlan> decide(const ZoneManager& zones) const {
    if (!zones.enabled()) return std::nullopt;
    const auto& hot = zones.hottest_logical();
    if (hot.user_writes < config_.wear_leveling_min_user_writes)
      return std::nullopt;
    const auto source = zones.physical_zone(
        static_cast<std::uint64_t>(hot.logical_zone) * config_.zone_size_pages);
    const auto& source_wear = zones.physical_wear(source);
    const auto* coolest = zones.least_worn_physical(source);
    if (!coolest || source_wear.max_pec <=
                         coolest->avg_pec + config_.wear_leveling_min_pec_delta)
      return std::nullopt;
    return WearLevelPlan{hot.logical_zone, source, coolest->physical_zone,
                         source_wear.max_pec, coolest->avg_pec};
  }

 private:
  const Config& config_;
};

}  // namespace hbfsim
