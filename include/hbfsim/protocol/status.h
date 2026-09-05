#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/common/types.h"
#include <cstdint>
#include <optional>
#include <string>

namespace hbfsim {

struct HbfErrorInfo {
  std::uint64_t logical_address = 0;
  std::optional<PhysicalAddr> physical_address;
  std::optional<std::uint32_t> retry_stage;
  std::optional<RetirementGranularity> retirement_granularity;
  std::string reason;
};

struct HbfResponse {
  std::uint64_t request_id = 0;
  HbfStatus status = HbfStatus::Success;
  HbfCompletionClass completion_class = HbfCompletionClass::Success;
  std::optional<std::uint8_t> protocol_status_code;
  SimTime completion_time = 0;
  std::uint64_t bytes_completed = 0;
  std::optional<HbfErrorInfo> error;

  [[nodiscard]] bool ok() const {
    return completion_class == HbfCompletionClass::Success ||
           completion_class == HbfCompletionClass::SuccessWithAdvisory;
  }
  [[nodiscard]] bool data_valid() const { return hbf_data_valid(status); }
  static HbfResponse success(std::uint64_t request_id,
                             SimTime completion_time = 0,
                             std::uint64_t bytes_completed = 0);
  static HbfResponse failure(std::uint64_t request_id, HbfStatus status,
                             HbfErrorInfo error,
                             SimTime completion_time = 0,
                             std::uint64_t bytes_completed = 0);
};

}  // namespace hbfsim
