#include "hbfsim/mapping/spec_block_addressing.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace hbfsim {

SpecBlockAddressing::SpecBlockAddressing(const Config& config,
                                         const HbfChannelDomain& channels)
    : config_(config), channels_(channels) {
  const auto total_planes = static_cast<std::uint64_t>(config_.stacks) *
                            config_.dies_per_stack * config_.planes_per_die;
  // Research profiles may intentionally use arbitrary topology for legacy
  // experiments. Their Spec API is never invoked, so preserve construction
  // while applying the strict divisibility requirement in Config::validate
  // for HBF Spec profiles.
  const auto lanes = std::max<std::uint64_t>(
      1, total_planes / std::max<std::uint32_t>(1, channels_.channel_count()));
  geometry_.r2_banks_per_die = std::max<std::uint32_t>(1, config_.banks_per_die);
  geometry_.r1_core_dies = static_cast<std::uint32_t>(
      std::max<std::uint64_t>(1, lanes / geometry_.r2_banks_per_die));
  geometry_.r3_pages_per_block = config_.pages_per_block;
  geometry_.r4_64b_units_per_page =
      static_cast<std::uint32_t>(std::max<std::uint64_t>(1, config_.page_size / 64));
  geometry_.r5_data_stripe_width =
      static_cast<std::uint32_t>(std::min<std::uint64_t>(lanes, UINT32_MAX));
}

SpecBlockReplayPlan SpecBlockAddressing::plan(
    std::uint64_t id, std::uint64_t global_address,
    HostRewriteReason reason) const {
  const auto channel_address = channels_.translate(global_address);
  const auto local_page = channel_address.local_address / config_.page_size;
  const auto in_page = channel_address.local_address % config_.page_size;
  const auto width = geometry_.r5_data_stripe_width;
  const auto block_span = static_cast<std::uint64_t>(width) *
                          geometry_.r3_pages_per_block;
  const auto block = local_page / block_span;
  const auto lane = local_page % width;
  const auto page0_local = block * width + lane;
  const auto page0_local_address = page0_local * config_.page_size;

  SpecBlockReplayPlan result;
  result.id = id;
  result.reason = reason;
  result.channel = channel_address.channel;
  result.failed_global_address = global_address;
  result.page0_global_address = channels_.global_address(
      {channel_address.channel, page0_local_address, 0, 0});
  result.failed_page = static_cast<std::uint32_t>(local_page / width %
                                                   geometry_.r3_pages_per_block);
  result.geometry = geometry_;
  result.page_global_addresses.reserve(geometry_.r3_pages_per_block);
  for (std::uint32_t page = 0; page < geometry_.r3_pages_per_block; ++page) {
    const auto local = (page0_local + static_cast<std::uint64_t>(page) * width) *
                           config_.page_size +
                       in_page;
    result.page_global_addresses.push_back(
        channels_.global_address({channel_address.channel, local, 0, 0}) - in_page);
  }
  return result;
}

}  // namespace hbfsim
