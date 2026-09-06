#include "hbfsim/protocol/admin.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace hbfsim {
namespace {

std::uint32_t log2_floor(std::uint32_t value) {
  return value <= 1 ? 0 : 31U - std::countl_zero(value);
}

}  // namespace

HbfRegisterFile::HbfRegisterFile(const Config& config,
                                 const NandTopology& topology,
                                 const NandMediaSystem& media)
    : config_(config), topology_(topology), media_(media) {
  const auto channels = config_.hbf_channel_count == 0
                            ? config_.stacks * config_.host_channels_per_stack
                            : config_.hbf_channel_count;
  bucc_.assign(channels, (2U << 6) | 1U);  // Host-controlled WL, enabled.
}

bool HbfRegisterFile::valid_channel(std::uint32_t channel) const {
  return channel < bucc_.size();
}

std::uint64_t HbfRegisterFile::buccap() const {
  // Fields implemented by this transaction-level device model: NCBB=2,
  // MOCS, host-controlled WLS, and capability-structure version 7.
  const auto mocs = std::min<std::uint32_t>(
      0x0D, log2_floor(config_.axi_max_outstanding_per_id));
  return (1ULL << 36) | (static_cast<std::uint64_t>(mocs) << 32) |
         (2ULL << 6) | 7ULL;
}

HbfRegisterFile::PecSummary HbfRegisterFile::pec_summary(
    std::uint32_t channel) const {
  const auto total_planes = topology_.plane_count();
  const auto channels = bucc_.size();
  const auto planes_per_channel = total_planes / channels;
  PecSummary result;
  const auto first_plane = static_cast<std::size_t>(channel) * planes_per_channel;
  for (std::size_t flat = first_plane;
       flat < first_plane + planes_per_channel; ++flat) {
    PhysicalAddr address;
    address.stack = static_cast<std::uint32_t>(
        flat / (config_.dies_per_stack * config_.planes_per_die));
    const auto within_stack = flat %
                              (config_.dies_per_stack * config_.planes_per_die);
    address.die = static_cast<std::uint32_t>(within_stack / config_.planes_per_die);
    address.plane = static_cast<std::uint32_t>(within_stack % config_.planes_per_die);
    for (std::uint32_t block = 0; block < config_.blocks_per_plane; ++block) {
      address.block = block;
      const auto pec = media_.block_erase_count(address);
      result.max = std::max(result.max, pec);
      result.total += pec;
      ++result.blocks;
      result.retired += media_.block_state(address) == BlockState::Bad;
    }
  }
  return result;
}

HbfRegisterResult HbfRegisterFile::read(std::uint32_t channel,
                                         std::uint32_t offset) const {
  if (!valid_channel(channel))
    return {HbfStatus::InvalidAddress, 0, "ADMIN_CHANNEL_OUT_OF_RANGE"};
  switch (offset) {
    case hbf_register::kBucCap:
      return {HbfStatus::Success, buccap(), {}};
    case hbf_register::kVersion:
      return {HbfStatus::Success, (7ULL << 16), {}};
    case hbf_register::kBucc:
      return {HbfStatus::Success, bucc_.at(channel), {}};
    case hbf_register::kBucStatus:
      return {HbfStatus::Success, (bucc_.at(channel) & 1U) ? 1ULL : 0ULL, {}};
    case hbf_register::kMaxPec:
      return {HbfStatus::Success, pec_summary(channel).max, {}};
    case hbf_register::kAvgPec: {
      const auto summary = pec_summary(channel);
      return {HbfStatus::Success,
              summary.blocks == 0 ? 0 : summary.total / summary.blocks, {}};
    }
    case hbf_register::kReducedCapacity:
      return {HbfStatus::Success,
              static_cast<std::uint64_t>(
                  pec_summary(channel).retired == 0 ? 0 : 1), {}};
    default:
      return {HbfStatus::InvalidAddress, 0, "UNIMPLEMENTED_HBF_REGISTER"};
  }
}

HbfRegisterResult HbfRegisterFile::write(std::uint32_t channel,
                                          std::uint32_t offset,
                                          std::uint64_t value) {
  if (!valid_channel(channel))
    return {HbfStatus::InvalidAddress, 0, "ADMIN_CHANNEL_OUT_OF_RANGE"};
  if (offset != hbf_register::kBucc)
    return {HbfStatus::InvalidUserField, 0, "REGISTER_IS_READ_ONLY"};
  const auto wls = static_cast<std::uint32_t>((value >> 6) & 0x3);
  if (wls != 1 && wls != 2)
    return {HbfStatus::InvalidUserField, 0, "INVALID_BUCC_WLS"};
  constexpr std::uint32_t writable = (0xFU << 24) | (0xFFU << 16) |
                                     (0x3U << 6) | 0x1U;
  bucc_.at(channel) = static_cast<std::uint32_t>(value) & writable;
  return {HbfStatus::Success, bucc_.at(channel), {}};
}

bool HbfRegisterFile::host_controlled_wear_leveling(
    std::uint32_t channel) const {
  return valid_channel(channel) && ((bucc_.at(channel) >> 6) & 0x3U) == 2U;
}

}  // namespace hbfsim
