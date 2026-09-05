#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/common/types.h"

#include <cstdint>
#include <vector>

namespace hbfsim {

struct ReadCacheEntry {
  bool valid = false;
  PhysicalAddr page;
  SimTime ready_at = 0;
  std::uint64_t last_use = 0;
};

class BankReadCache {
 public:
  BankReadCache() = default;
  explicit BankReadCache(std::uint32_t entries) : entries_(entries) {}

  bool lookup(const PhysicalAddr& page, SimTime now);
  bool fill(const PhysicalAddr& page, SimTime ready_at);
  void invalidate_page(const PhysicalAddr& page);
  void invalidate_block(const PhysicalAddr& block);
  std::size_t size() const { return entries_.size(); }

 private:
  std::vector<ReadCacheEntry> entries_;
  std::uint64_t lru_clock_ = 0;
};

}  // namespace hbfsim
