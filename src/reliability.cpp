#include "hbfsim/core.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hbfsim {

ReliabilityModel::ReliabilityModel(const Config& config)
    : config_(config), random_(config.random_seed) {}

bool ReliabilityModel::program_failed() {
  if (config_.program_failure_rate <= 0.0) return false;
  if (config_.program_failure_rate >= 1.0) return true;
  return std::bernoulli_distribution(config_.program_failure_rate)(random_);
}

ReadErrorResult ReliabilityModel::read_result(std::uint64_t bytes,
                                               std::uint32_t retry) {
  const double retry_scale = std::pow(config_.retry_ber_multiplier, retry);
  const double mean_errors = static_cast<double>(bytes) * 8.0 *
                             config_.raw_bit_error_rate * retry_scale;
  if (mean_errors <= 0.0) return {};
  const auto sampled =
      std::poisson_distribution<std::uint64_t>(mean_errors)(random_);
  const auto max_bits = bytes > std::numeric_limits<std::uint64_t>::max() / 8
                            ? std::numeric_limits<std::uint64_t>::max()
                            : bytes * 8;
  const auto errors = std::min(sampled, max_bits);
  if (errors == 0) return {};
  return {errors <= config_.ecc_correctable_bits
              ? ReadErrorStatus::Corrected
              : ReadErrorStatus::Uncorrectable,
          errors};
}

}  // namespace hbfsim
