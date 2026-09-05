#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/config/config.h"
#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace hbfsim {

struct StripeId {
  std::uint64_t physical_id = std::numeric_limits<std::uint64_t>::max();
  std::uint32_t generation = 0;

  bool valid() const {
    return physical_id != std::numeric_limits<std::uint64_t>::max() &&
           generation != 0;
  }
  friend bool operator==(const StripeId&, const StripeId&) = default;
};

class LazyBitmap {
 public:
  bool test(std::uint32_t bit) const;
  void set(std::uint32_t bit, std::uint32_t bit_count);
  void clear(std::uint32_t bit);
  bool any() const;
  std::uint32_t count() const;
  void reset() { words_.clear(); }
  std::size_t allocated_words() const { return words_.size(); }

 private:
  std::vector<std::uint64_t> words_;
};

struct ExtentRun {
  std::uint32_t physical_start_slot = 0;
  std::uint32_t slot_count = 0;
  std::uint64_t logical_base_lpn = 0;
};

struct StripeDescriptor {
  StripeId id;
  std::uint64_t logical_base_lpn = 0;
  std::uint32_t next_program_slot = 0;
  std::uint32_t valid_slots = 0;
  std::uint32_t reserved_programs = 0;
  std::uint32_t erased_lanes = 0;
  SimTime retention_since = 0;
  StripeState state = StripeState::Free;
  LazyBitmap valid_bitmap;
  LazyBitmap invalid_bitmap;
  LazyBitmap failed_bitmap;
  LazyBitmap hole_bitmap;
  LazyBitmap reserved_bitmap;
  LazyBitmap erased_lane_bitmap;
  std::vector<ExtentRun> extent_runs;
  std::unordered_map<std::uint32_t, std::uint64_t> exceptions;
};

struct ProgramFailureNotice {
  StripeId stripe;
  std::uint32_t failed_slot = 0;
  PhysicalAddr failed_ppa;
  std::uint32_t committed_slots = 0;
};

class StripeMappingTable {
 public:
  explicit StripeMappingTable(const Config& config);

  std::uint32_t stripe_width() const { return stripe_width_; }
  std::uint32_t stripe_capacity() const { return stripe_capacity_; }
  std::uint32_t parallelism_group_count() const {
    return parallelism_group_count_;
  }
  std::uint32_t parallelism_group(const StripeId& stripe) const;
  StripeId allocate(std::uint64_t logical_base_lpn);
  StripeId allocate_replacement(std::uint64_t logical_base_lpn);
  void set_zone_resolver(std::uint32_t zone_count,
                         std::function<std::uint32_t(std::uint64_t)> resolver) {
    zone_count_ = zone_count;
    zone_resolver_ = std::move(resolver);
  }
  PhysicalAddr preview_program(std::uint64_t lpn) const;
  PhysicalAddr reserve_program(std::uint64_t lpn);
  PhysicalAddr reserve_program(const StripeId& destination,
                               std::uint64_t lpn);
  void reserve_hole(const StripeId& destination, std::uint64_t lpn);
  void commit_program(std::uint64_t lpn, const PhysicalAddr& paddr,
                      SimTime now = 0);
  ProgramFailureNotice fail_program(std::uint64_t lpn,
                                    const PhysicalAddr& paddr);
  void invalidate(std::uint64_t lpn);
  std::optional<PhysicalAddr> lookup(std::uint64_t lpn) const;
  std::optional<std::uint64_t> reverse_lookup(
      const PhysicalAddr& paddr, std::uint32_t expected_generation) const;
  PhysicalAddr address_for(const StripeId& stripe,
                           std::uint32_t slot) const;
  std::uint32_t slot_of(const PhysicalAddr& paddr) const;
  void seal(const StripeId& stripe);
  void begin_migration(const StripeId& source);
  void remap_commit(const StripeId& source, const StripeId& destination);
  void abort_migration(const StripeId& destination);
  void on_erase(const PhysicalAddr& block_addr);
  bool retire_stripe(const PhysicalAddr& block_addr);
  bool validate_generation(const PhysicalAddr& paddr) const;
  const StripeDescriptor& descriptor(const StripeId& stripe) const;
  std::optional<StripeId> active_stripe(std::uint64_t lpn) const;
  std::size_t active_mapping_count() const { return active_.size(); }
  std::size_t free_stripe_count() const { return free_stripes_.size(); }
  std::size_t total_stripe_count() const { return descriptors_.size(); }
  bool physical_stripe_retired(std::uint64_t physical_id) const {
    return physical_id >= descriptors_.size() ||
           descriptors_.at(physical_id).state == StripeState::Bad;
  }
  std::size_t usable_stripe_count() const { return usable_stripes_; }
  std::size_t host_visible_stripe_count() const {
    return host_visible_stripes_;
  }
  std::vector<StripeId> active_stripes() const;

 private:
  StripeId allocate_internal(std::uint64_t logical_base_lpn,
                             bool publish);
  StripeDescriptor& mutable_descriptor(const StripeId& stripe);
  std::uint64_t logical_base(std::uint64_t lpn) const;
  PhysicalAddr preview_for(const StripeId& stripe, std::uint64_t lpn) const;
  PhysicalAddr address_from_geometry(std::uint64_t physical,
                                     std::uint32_t generation,
                                     std::uint32_t slot) const;
  const Config& config_;
  std::uint32_t stripe_width_ = 0;
  std::uint32_t stripe_capacity_ = 0;
  std::uint32_t parallelism_group_count_ = 0;
  std::vector<StripeDescriptor> descriptors_;
  std::vector<std::uint32_t> generations_;
  std::deque<std::uint64_t> free_stripes_;
  std::map<std::uint64_t, StripeId> active_;
  std::size_t host_visible_stripes_ = 0;
  std::size_t usable_stripes_ = 0;
  std::uint32_t zone_count_ = 0;
  std::function<std::uint32_t(std::uint64_t)> zone_resolver_;
};

}  // namespace hbfsim
