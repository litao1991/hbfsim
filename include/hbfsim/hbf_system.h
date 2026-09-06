#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/controller/base_die.h"
#include "hbfsim/extension/copy_engine.h"
#include "hbfsim/controller/interconnect.h"
#include "hbfsim/management/host_gc.h"
#include "hbfsim/management/refresh.h"
#include "hbfsim/management/spec_zones.h"
#include "hbfsim/management/zones.h"
#include "hbfsim/management/wear_level.h"
#include "hbfsim/mapping/mapper.h"
#include "hbfsim/mapping/spec_block_addressing.h"
#include "hbfsim/media/nand_media.h"
#include "hbfsim/media/reliability.h"
#include "hbfsim/media/topology.h"
#include "hbfsim/protocol/axi.h"
#include "hbfsim/protocol/admin.h"
#include "hbfsim/protocol/channel.h"
#include "hbfsim/protocol/dlu.h"
#include "hbfsim/protocol/frontend.h"
#include "hbfsim/protocol/validator.h"

namespace hbfsim {

struct HbfSystemCapabilities {
  bool spec_profile = false;
  bool ai_system_semantics = false;
  bool transaction_protocol = true;
  bool research_stripe_mapping = true;
  bool research_copy_gc = true;
  bool research_migration_recovery = true;
};

// Composition root for the HBF device model. Simulator owns time and events;
// HbfSystem owns protocol, topology, media, reliability, and maintenance.
class HbfSystem {
 public:
  explicit HbfSystem(const Config& config);

  SimulationProfile profile() const { return profile_; }
  ProtocolAbstraction protocol_abstraction() const {
    return protocol_abstraction_;
  }
  const HbfSystemCapabilities& capabilities() const { return capabilities_; }
  const NandTopology& topology() const { return topology_; }
  NandMediaSystem& media() { return media_; }
  const NandMediaSystem& media() const { return media_; }
  BaseDieController& controller() { return controller_; }
  const BaseDieController& controller() const { return controller_; }
  CopyEngine& copy_engine() { return copy_engine_; }
  const CopyEngine& copy_engine() const { return copy_engine_; }
  HostRewriteEngine& host_rewrite_engine() { return host_rewrite_engine_; }
  const HostRewriteEngine& host_rewrite_engine() const {
    return host_rewrite_engine_;
  }
  HostReplayManager& replay_manager() { return host_rewrite_engine_; }
  const HostReplayManager& replay_manager() const { return host_rewrite_engine_; }
  AddressMapper& mapper() { return mapper_; }
  const AddressMapper& mapper() const { return mapper_; }
  HostRouter& host_router() { return host_router_; }
  const HostRouter& host_router() const { return host_router_; }
  const HbfChannelDomain& channels() const { return channels_; }
  const SpecBlockAddressing& spec_block_addressing() const {
    return spec_block_addressing_;
  }
  HbfRegisterFile& registers() { return registers_; }
  const HbfRegisterFile& registers() const { return registers_; }
  SpecZoneManager& spec_zones() { return spec_zones_; }
  const SpecZoneManager& spec_zones() const { return spec_zones_; }
  const HbfProtocolValidator& protocol_validator() const {
    return frontend_.validator();
  }
  ProtocolFrontend& frontend() { return frontend_; }
  const ProtocolFrontend& frontend() const { return frontend_; }
  AxiOrderTracker& axi() { return frontend_.axi(); }
  const AxiOrderTracker& axi() const { return frontend_.axi(); }
  DluAssembler& dlu_assembler() { return frontend_.dlu_assembler(); }
  const DluAssembler& dlu_assembler() const {
    return frontend_.dlu_assembler();
  }
  ReliabilityModel& reliability() { return reliability_; }
  const ReliabilityModel& reliability() const { return reliability_; }
  HostGcManager& host_gc_manager() { return host_gc_manager_; }
  const HostGcManager& host_gc_manager() const { return host_gc_manager_; }
  RefreshManager& refresh_manager() { return refresh_manager_; }
  const RefreshManager& refresh_manager() const { return refresh_manager_; }
  ZoneManager& zones() { return zones_; }
  const ZoneManager& zones() const { return zones_; }
  HostWearLevelManager& wear_level_manager() { return wear_level_manager_; }
  const HostWearLevelManager& wear_level_manager() const {
    return wear_level_manager_;
  }

 private:
  SimulationProfile profile_ = SimulationProfile::MediaResearch;
  ProtocolAbstraction protocol_abstraction_ =
      ProtocolAbstraction::Transaction;
  HbfSystemCapabilities capabilities_;
  NandTopology topology_;
  HbfChannelDomain channels_;
  ProtocolFrontend frontend_;
  SpecZoneManager spec_zones_;
  AddressMapper mapper_;
  SpecBlockAddressing spec_block_addressing_;
  HostRouter host_router_;
  ReliabilityModel reliability_;
  NandMediaSystem media_;
  HbfRegisterFile registers_;
  BaseDieController controller_;
  CopyEngine copy_engine_;
  HostRewriteEngine host_rewrite_engine_;
  HostGcManager host_gc_manager_;
  RefreshManager refresh_manager_;
  ZoneManager zones_;
  HostWearLevelManager wear_level_manager_;
};

}  // namespace hbfsim
