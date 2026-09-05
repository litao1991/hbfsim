#pragma once

#include "hbfsim/config/config.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace hbfsim {

struct ZoneWear {
  std::uint32_t logical_zone = 0;
  std::uint32_t physical_zone = 0;
  std::uint64_t writes = 0;
  std::uint64_t erases = 0;
  std::uint64_t pec_samples = 0;
  double avg_pec = 0.0;
  double pec_m2 = 0.0;
  std::uint32_t max_pec = 0;
  double pec_variance() const {
    return pec_samples < 2 ? 0.0 : pec_m2 / (pec_samples - 1);
  }
};

class ZoneManager {
 public:
  explicit ZoneManager(const Config& config) : config_(config) {
    if (config_.zone_count == 0) return;
    zones_.reserve(config_.zone_count);
    for (std::uint32_t id = 0; id < config_.zone_count; ++id)
      zones_.push_back({id, id, 0, 0});
  }
  bool enabled() const { return !zones_.empty(); }
  void record_write(std::uint64_t lpn) { if (enabled()) ++zone(lpn).writes; }
  void record_erase(std::uint64_t lpn) { if (enabled()) ++zone(lpn).erases; }
  void record_block_erase(std::uint64_t lpn, std::uint32_t erase_count) {
    if (!enabled()) return;
    auto& value = zone(lpn);
    ++value.erases;
    ++value.pec_samples;
    const auto delta = static_cast<double>(erase_count) - value.avg_pec;
    value.avg_pec += delta / value.pec_samples;
    value.pec_m2 += delta * (static_cast<double>(erase_count) - value.avg_pec);
    value.max_pec = std::max(value.max_pec, erase_count);
  }
  void remap(std::uint32_t logical_zone, std::uint32_t physical_zone) {
    if (logical_zone >= zones_.size() || physical_zone >= zones_.size())
      throw std::out_of_range("zone remap exceeds configured zone count");
    zones_.at(logical_zone).physical_zone = physical_zone;
  }
  const ZoneWear& hottest() const {
    return *std::max_element(zones_.begin(), zones_.end(),
        [](const auto& a, const auto& b) { return a.writes < b.writes; });
  }
  const ZoneWear& coldest() const {
    return *std::min_element(zones_.begin(), zones_.end(),
        [](const auto& a, const auto& b) { return a.writes < b.writes; });
  }
  std::uint32_t physical_zone(std::uint64_t lpn) const {
    return enabled() ? zone(lpn).physical_zone : 0;
  }
  const std::vector<ZoneWear>& zones() const { return zones_; }
 private:
  ZoneWear& zone(std::uint64_t lpn) {
    if (config_.zone_size_pages == 0) throw std::logic_error("zone size is zero");
    return zones_.at((lpn / config_.zone_size_pages) % zones_.size());
  }
  const ZoneWear& zone(std::uint64_t lpn) const {
    if (config_.zone_size_pages == 0) throw std::logic_error("zone size is zero");
    return zones_.at((lpn / config_.zone_size_pages) % zones_.size());
  }
  const Config& config_;
  std::vector<ZoneWear> zones_;
};
}  // namespace hbfsim
