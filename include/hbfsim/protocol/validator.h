#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/protocol/channel.h"
#include "hbfsim/protocol/request.h"
#include <optional>
#include <string>

namespace hbfsim {

struct HbfValidationResult {
  HbfStatus status = HbfStatus::Success;
  std::string reason;
  std::optional<HbfChannelAddress> address;

  [[nodiscard]] bool ok() const { return status == HbfStatus::Success; }
};

class HbfProtocolValidator {
 public:
  HbfProtocolValidator(const Config& config,
                       const HbfChannelDomain& channels)
      : config_(config), channels_(channels) {}
  HbfValidationResult validate(const TraceEntry& entry) const;

 private:
  const Config& config_;
  const HbfChannelDomain& channels_;
};

}  // namespace hbfsim
