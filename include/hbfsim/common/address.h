#pragma once

#include <cstdint>
#include <limits>

namespace hbfsim {

struct PhysicalAddr {
  std::uint32_t stack = 0;
  std::uint32_t die = 0;
  std::uint32_t plane = 0;
  std::uint32_t block = 0;
  std::uint32_t page = 0;
  std::uint64_t offset = 0;
  std::uint32_t data_port = 0;
  std::uint64_t physical_stripe =
      std::numeric_limits<std::uint64_t>::max();
  std::uint32_t generation = 0;
  std::uint32_t channel = 0;
  std::uint32_t bank = 0;
};

}  // namespace hbfsim
