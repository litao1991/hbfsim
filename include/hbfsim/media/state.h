#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/common/types.h"
#include "hbfsim/media/read_cache.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace hbfsim {

struct BlockMeta {
  BlockState state = BlockState::Free;
  std::uint32_t erase_count = 0;
  std::uint32_t next_program_page = 0;
  std::uint32_t valid_pages = 0;
  std::uint32_t invalid_pages = 0;
  bool bad = false;
  SimTime last_program_time = 0;
  SimTime last_refresh_time = 0;
  SimTime ready_at = 0;
  std::vector<std::uint64_t> valid_bitmap;
  std::vector<std::uint64_t> invalid_bitmap;
  std::vector<std::uint64_t> failed_bitmap;
};

struct DieState {
  SimTime ready_at = 0;
  SimTime command_ready_at = 0;
};

struct BankState {
  SimTime command_ready_at = 0;
  BankReadCache read_cache;
  BankSenseQueue sense_queue;
};

struct PlaneMediaState {
  SimTime ready_at = 0;
  bool data_register_busy = false;
  std::vector<BlockMeta> blocks;
};

struct PlaneControllerState {
  static constexpr std::size_t kSourceCount = 6;
  using SourceQueues =
      std::array<std::deque<std::uint64_t>, kSourceCount>;
  bool busy = false;
  SourceQueues reads;
  SourceQueues writes;
  SourceQueues erases;
  SourceQueues refreshes;
  std::uint32_t consecutive_reads = 0;
  bool suspend_pending = false;
  std::optional<std::uint64_t> active_subrequest;
  std::optional<std::uint64_t> suspended_subrequest;
  std::optional<std::uint64_t> cached_write;
};

}  // namespace hbfsim
