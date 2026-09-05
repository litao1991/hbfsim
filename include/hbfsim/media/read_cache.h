#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/common/types.h"

#include <cstdint>
#include <deque>
#include <stdexcept>
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

// Ordered ownership domain for NAND Sense. v0.5.0 deliberately keeps this
// separate from the read cache and command-ready timing: a future Batch Read
// scheduler can enqueue one group while legacy single reads retain their
// established path.
class BankSenseQueue {
 public:
  using EntryId = std::uint64_t;

  void enqueue(EntryId id) {
    if (!entries_.empty() && entries_.back() == id)
      throw std::logic_error("duplicate bank sense queue entry");
    entries_.push_back(id);
  }
  EntryId front() const {
    if (entries_.empty()) throw std::logic_error("bank sense queue is empty");
    return entries_.front();
  }
  void pop_front(EntryId id) {
    if (entries_.empty() || entries_.front() != id)
      throw std::logic_error("bank sense queue order violation");
    entries_.pop_front();
  }
  bool empty() const { return entries_.empty(); }
  std::size_t size() const { return entries_.size(); }

 private:
  std::deque<EntryId> entries_;
};

}  // namespace hbfsim
