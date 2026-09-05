#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/media/state.h"
#include "hbfsim/media/topology.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hbfsim {

class NandMediaSystem {
 public:
  NandMediaSystem(const Config& config, const NandTopology& topology);

  Plane& plane(const PhysicalAddr& address);
  const Plane& plane(const PhysicalAddr& address) const;
  DieState& die(const PhysicalAddr& address);
  const DieState& die(const PhysicalAddr& address) const;
  BankState& bank(const PhysicalAddr& address);
  const BankState& bank(const PhysicalAddr& address) const;

  PageState page_state(const PhysicalAddr& address) const;
  BlockState block_state(const PhysicalAddr& address) const;
  SimTime block_ready_at(const PhysicalAddr& address) const;
  std::uint32_t block_erase_count(const PhysicalAddr& address) const;
  SimTime die_ready_at(const PhysicalAddr& address) const;

  bool read_cache_lookup(const PhysicalAddr& address, SimTime now);
  bool read_cache_fill(const PhysicalAddr& address, SimTime ready_at);
  void invalidate_read_cache_page(const PhysicalAddr& address);
  void invalidate_read_cache_block(const PhysicalAddr& address);

  void mark_erased(const PhysicalAddr& address);
  void set_transient_page_state(const PhysicalAddr& address,
                                PageState state);
  void clear_transient_page_state(const PhysicalAddr& address);
  void materialize_initialized_page(const PhysicalAddr& address);
  bool retire_block(const PhysicalAddr& address);

  const NandTopology& topology() const { return topology_; }

 private:
  std::uint64_t page_key(const PhysicalAddr& address) const;
  std::uint64_t block_key(const PhysicalAddr& address) const;

  const Config& config_;
  const NandTopology& topology_;
  std::vector<Plane> planes_;
  std::vector<DieState> dies_;
  std::vector<BankState> banks_;
  std::unordered_map<std::uint64_t, PageState> transient_page_states_;
  std::unordered_set<std::uint64_t> erased_blocks_;
};

}  // namespace hbfsim
