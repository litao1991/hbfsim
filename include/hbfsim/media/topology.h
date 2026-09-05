#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/config/config.h"

#include <cstddef>
#include <cstdint>

namespace hbfsim {

class NandTopology {
 public:
  explicit NandTopology(const Config& config) : config_(config) {}

  std::uint32_t flat_plane(const PhysicalAddr& address) const;
  std::uint32_t flat_die(const PhysicalAddr& address) const;
  std::uint32_t flat_bank(const PhysicalAddr& address) const;
  std::uint32_t bank_of_plane(std::uint32_t plane) const;
  std::size_t plane_count() const;
  std::size_t die_count() const;
  std::size_t bank_count() const;

 private:
  const Config& config_;
};

}  // namespace hbfsim
