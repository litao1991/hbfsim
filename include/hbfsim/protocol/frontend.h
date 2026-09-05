#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/protocol/axi.h"
#include "hbfsim/protocol/channel.h"
#include "hbfsim/protocol/dlu.h"
#include "hbfsim/protocol/request.h"
#include "hbfsim/protocol/status.h"
#include "hbfsim/protocol/validator.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace hbfsim {

struct FrontendAdmission {
  Request request;
  std::optional<HbfResponse> rejection;

  [[nodiscard]] bool accepted() const { return !rejection.has_value(); }
};

class ProtocolFrontend {
 public:
  ProtocolFrontend(const Config& config, const HbfChannelDomain& channels);

  FrontendAdmission admit(const TraceEntry& entry,
                          std::uint64_t request_id,
                          bool measured);
  std::vector<HbfResponse> complete(const Request& request);

  const HbfProtocolValidator& validator() const { return validator_; }
  AxiOrderTracker& axi() { return axi_; }
  const AxiOrderTracker& axi() const { return axi_; }
  DluAssembler& dlu_assembler() { return dlu_assembler_; }
  const DluAssembler& dlu_assembler() const { return dlu_assembler_; }

 private:
  const Config& config_;
  HbfProtocolValidator validator_;
  AxiOrderTracker axi_;
  DluAssembler dlu_assembler_;
};

}  // namespace hbfsim
