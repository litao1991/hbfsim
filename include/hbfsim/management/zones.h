#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/config/config.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace hbfsim {

struct LogicalZoneStats {
  std::uint32_t logical_zone = 0;
  std::uint64_t user_writes = 0;
};

struct PhysicalZoneWear {
  std::uint32_t physical_zone = 0;
  std::uint64_t physical_programs = 0;
  std::uint64_t erases = 0;
  std::uint64_t block_count = 0;
  double avg_pec = 0.0;
  double pec_m2 = 0.0;
  std::uint32_t max_pec = 0;
  double pec_variance() const {
    return block_count < 2 ? 0.0 : pec_m2 / (block_count - 1);
  }
};

// Logical hotness, logical→physical placement, and physical P/E state have
// deliberately separate owners. A physical-zone move is always a bijection.
class ZoneManager {
 public:
  explicit ZoneManager(const Config& config) : config_(config) {
    if (config_.zone_count == 0) return;
    logical_.reserve(config_.zone_count);
    physical_.reserve(config_.zone_count);
    logical_to_physical_.reserve(config_.zone_count);
    physical_to_logical_.reserve(config_.zone_count);
    block_pec_.resize(config_.zone_count);
    for (std::uint32_t id = 0; id < config_.zone_count; ++id) {
      logical_.push_back({id, 0});
      physical_.push_back({id, 0, 0, 0, 0.0, 0.0, 0});
      logical_to_physical_.push_back(id);
      physical_to_logical_.push_back(id);
    }
  }

  bool enabled() const { return !logical_.empty(); }

  std::uint32_t logical_zone_of(std::uint64_t lpn) const {
    if (!enabled()) return 0;
    const auto zone = lpn / config_.zone_size_pages;
    if (zone >= logical_.size())
      throw std::out_of_range("LPN_OUTSIDE_LOGICAL_ZONE_RANGE");
    return static_cast<std::uint32_t>(zone);
  }

  void record_user_write(std::uint64_t lpn) {
    if (enabled()) ++logical_.at(logical_zone_of(lpn)).user_writes;
  }
  void record_write(std::uint64_t lpn) { record_user_write(lpn); }
  void record_erase(std::uint64_t) {}

  void record_physical_program(const PhysicalAddr& paddr) {
    if (!enabled() ||
        paddr.physical_stripe == std::numeric_limits<std::uint64_t>::max())
      return;
    ++physical_.at(physical_zone_for_stripe(paddr.physical_stripe))
          .physical_programs;
  }

  void record_physical_erase(const PhysicalAddr& paddr,
                             std::uint32_t erase_count) {
    if (!enabled() ||
        paddr.physical_stripe == std::numeric_limits<std::uint64_t>::max())
      return;
    const auto zone = physical_zone_for_stripe(paddr.physical_stripe);
    block_pec_.at(zone)[block_key(paddr)] = erase_count;
    ++physical_.at(zone).erases;
    recompute_wear(zone);
  }

  void remap(std::uint32_t logical_zone, std::uint32_t physical_zone) {
    if (logical_zone >= logical_.size() || physical_zone >= physical_.size())
      throw std::out_of_range("zone remap exceeds configured zone count");
    const auto old_physical = logical_to_physical_.at(logical_zone);
    const auto other_logical = physical_to_logical_.at(physical_zone);
    logical_to_physical_.at(logical_zone) = physical_zone;
    physical_to_logical_.at(physical_zone) = logical_zone;
    logical_to_physical_.at(other_logical) = old_physical;
    physical_to_logical_.at(old_physical) = other_logical;
  }

  std::uint32_t physical_zone(std::uint64_t lpn) const {
    return enabled() ? logical_to_physical_.at(logical_zone_of(lpn)) : 0;
  }
  std::uint32_t physical_zone_for_stripe(std::uint64_t stripe) const {
    if (!enabled()) return 0;
    const auto total = total_physical_stripes();
    if (stripe >= total) throw std::out_of_range("STRIPE_OUTSIDE_PHYSICAL_ZONE");
    return static_cast<std::uint32_t>(stripe * physical_.size() / total);
  }
  std::uint32_t logical_owner(std::uint32_t physical_zone) const {
    return physical_to_logical_.at(physical_zone);
  }
  const LogicalZoneStats& hottest_logical() const {
    return *std::max_element(logical_.begin(), logical_.end(),
        [](const auto& a, const auto& b) { return a.user_writes < b.user_writes; });
  }
  const PhysicalZoneWear& physical_wear(std::uint32_t id) const {
    return physical_.at(id);
  }
  const PhysicalZoneWear* least_worn_physical(std::uint32_t exclude) const {
    const PhysicalZoneWear* result = nullptr;
    for (const auto& candidate : physical_) {
      if (candidate.physical_zone == exclude) continue;
      if (!result || candidate.avg_pec < result->avg_pec ||
          (candidate.avg_pec == result->avg_pec &&
           candidate.max_pec < result->max_pec))
        result = &candidate;
    }
    return result;
  }
  const std::vector<LogicalZoneStats>& logical_zones() const { return logical_; }
  const std::vector<PhysicalZoneWear>& physical_zones() const { return physical_; }

 private:
  std::uint64_t total_physical_stripes() const {
    const auto planes = static_cast<std::uint64_t>(config_.stacks) *
                        config_.dies_per_stack * config_.planes_per_die;
    auto width = planes;
    if (config_.stripe_scope == StripeScope::Stack)
      width = static_cast<std::uint64_t>(config_.dies_per_stack) *
              config_.planes_per_die;
    else if (config_.stripe_scope == StripeScope::Custom)
      width = config_.stripe_lanes;
    return (planes / width) * config_.blocks_per_plane;
  }
  std::uint64_t block_key(const PhysicalAddr& paddr) const {
    const auto lane = (static_cast<std::uint64_t>(paddr.stack) *
                       config_.dies_per_stack + paddr.die) *
                          config_.planes_per_die +
                      paddr.plane;
    return lane * config_.blocks_per_plane + paddr.block;
  }
  void recompute_wear(std::uint32_t zone) {
    auto& result = physical_.at(zone);
    const auto& values = block_pec_.at(zone);
    result.block_count = values.size();
    result.avg_pec = 0.0;
    result.pec_m2 = 0.0;
    result.max_pec = 0;
    std::uint64_t seen = 0;
    for (const auto& [_, value] : values) {
      ++seen;
      const auto delta = static_cast<double>(value) - result.avg_pec;
      result.avg_pec += delta / seen;
      result.max_pec = std::max(result.max_pec, value);
    }
    for (const auto& [_, value] : values) {
      const auto delta = static_cast<double>(value) - result.avg_pec;
      result.pec_m2 += delta * delta;
    }
  }

  const Config& config_;
  std::vector<LogicalZoneStats> logical_;
  std::vector<PhysicalZoneWear> physical_;
  std::vector<std::uint32_t> logical_to_physical_;
  std::vector<std::uint32_t> physical_to_logical_;
  std::vector<std::unordered_map<std::uint64_t, std::uint32_t>> block_pec_;
};

}  // namespace hbfsim
