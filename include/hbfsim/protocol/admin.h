#pragma once

#include "hbfsim/common/types.h"
#include "hbfsim/config/config.h"
#include "hbfsim/media/nand_media.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hbfsim {

enum class HbfAdminOpcode : std::uint8_t {
  ZoneRemapping = 0x08,
  ReducedCapacity = 0x0A,
  RegisterReadWrite = 0x20,
};

namespace hbf_register {
constexpr std::uint32_t kBucCap = 0x0000;
constexpr std::uint32_t kVersion = 0x0008;
constexpr std::uint32_t kBucc = 0x000C;
constexpr std::uint32_t kBucStatus = 0x0010;
constexpr std::uint32_t kMaxPec = 0x0144;
constexpr std::uint32_t kAvgPec = 0x0148;
constexpr std::uint32_t kReducedCapacity = 0x014C;
}  // namespace hbf_register

struct HbfRegisterResult {
  HbfStatus status = HbfStatus::Success;
  std::uint64_t value = 0;
  std::string reason;
  [[nodiscard]] bool ok() const { return status == HbfStatus::Success; }
};

struct HbfAdminResult {
  HbfStatus status = HbfStatus::Success;
  std::string reason;
  [[nodiscard]] bool ok() const { return status == HbfStatus::Success; }
};

struct HbfAdminCommand {
  std::uint32_t channel = 0;
  HbfAdminOpcode opcode = HbfAdminOpcode::RegisterReadWrite;
  std::uint32_t register_offset = 0;
  std::uint64_t register_value = 0;
  bool register_write = false;
  std::vector<std::pair<std::uint16_t, std::uint16_t>> zone_swaps;
};

// Transaction-level control-plane state. Register access is exposed directly
// through Simulator APIs; a future flit model can adapt the same operations.
class HbfRegisterFile {
 public:
  HbfRegisterFile(const Config& config, const NandTopology& topology,
                  const NandMediaSystem& media);

  HbfRegisterResult read(std::uint32_t channel, std::uint32_t offset) const;
  HbfRegisterResult write(std::uint32_t channel, std::uint32_t offset,
                          std::uint64_t value);
  bool host_controlled_wear_leveling(std::uint32_t channel) const;

 private:
  struct PecSummary {
    std::uint32_t max = 0;
    std::uint64_t total = 0;
    std::uint64_t blocks = 0;
    std::uint64_t retired = 0;
  };
  bool valid_channel(std::uint32_t channel) const;
  PecSummary pec_summary(std::uint32_t channel) const;
  std::uint64_t buccap() const;

  const Config& config_;
  const NandTopology& topology_;
  const NandMediaSystem& media_;
  std::vector<std::uint32_t> bucc_;
};

}  // namespace hbfsim
