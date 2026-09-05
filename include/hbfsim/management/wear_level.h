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
  std::optional<WearLevelPlan> decide(const ZoneManager& zones) const {
    if (!zones.enabled()) return std::nullopt;
    const auto& hot = zones.hottest();
    const ZoneWear* coolest = nullptr;
    for (const auto& candidate : zones.zones()) {
      if (candidate.physical_zone == hot.physical_zone) continue;
      if (!coolest || candidate.avg_pec < coolest->avg_pec ||
          (candidate.avg_pec == coolest->avg_pec &&
           candidate.max_pec < coolest->max_pec))
        coolest = &candidate;
    }
    if (!coolest || hot.writes == 0) return std::nullopt;
    return WearLevelPlan{hot.logical_zone, hot.physical_zone,
                         coolest->physical_zone, hot.max_pec,
                         coolest->avg_pec};
  }
};

}  // namespace hbfsim
