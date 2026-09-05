#pragma once

#include "hbfsim/common/types.h"
#include "hbfsim/config/config.h"
#include <cstdint>
#include <random>

namespace hbfsim {

struct ReadErrorResult {
  ReadErrorStatus status = ReadErrorStatus::Clean;
  std::uint64_t bit_errors = 0;
};

class ReliabilityModel {
 public:
  explicit ReliabilityModel(const Config& config);
  bool program_failed(std::uint32_t erase_count = 0);
  bool erase_failed(std::uint32_t erase_count = 0);
  ReadErrorResult read_result(std::uint64_t bytes, std::uint32_t retry,
                              std::uint32_t erase_count = 0,
                              std::uint64_t read_count = 0,
                              SimTime retention_age_ns = 0);

 private:
  const Config& config_;
  std::mt19937_64 random_;
  std::uint64_t injected_program_failures_ = 0;
};

}  // namespace hbfsim
