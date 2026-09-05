#include "hbfsim/core.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hbfsim {
namespace {

double wear_probability(double base, double per_erase,
                        std::uint32_t erase_count) {
  return std::clamp(base + per_erase * static_cast<double>(erase_count),
                    0.0, 1.0);
}

}  // namespace

ReliabilityModel::ReliabilityModel(const Config& config)
    : config_(config), random_(config.random_seed) {}

bool ReliabilityModel::program_failed(std::uint32_t erase_count) {
  if (config_.program_failure_budget != 0 &&
      injected_program_failures_ >= config_.program_failure_budget)
    return false;
  const auto probability = wear_probability(
      config_.program_failure_rate,
      config_.program_failure_rate_per_erase, erase_count);
  if (probability <= 0.0) return false;
  const bool failed = probability >= 1.0 ||
                      std::bernoulli_distribution(probability)(random_);
  if (failed) ++injected_program_failures_;
  return failed;
}

bool ReliabilityModel::erase_failed(std::uint32_t erase_count) {
  const auto probability = wear_probability(
      config_.erase_failure_rate, config_.erase_failure_rate_per_erase,
      erase_count);
  return probability >= 1.0 ||
         (probability > 0.0 &&
          std::bernoulli_distribution(probability)(random_));
}

ReadErrorResult ReliabilityModel::read_result(std::uint64_t bytes,
                                               std::uint32_t retry,
                                               std::uint32_t erase_count) {
  const double retry_scale = std::pow(config_.retry_ber_multiplier, retry);
  const auto rber = wear_probability(
      config_.raw_bit_error_rate, config_.raw_bit_error_rate_per_erase,
      erase_count);
  const double mean_errors = static_cast<double>(bytes) * 8.0 *
                             rber * retry_scale;
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
