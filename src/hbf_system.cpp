#include "hbfsim/hbf_system.h"

#include <stdexcept>
#include <utility>

namespace hbfsim {
namespace {

HbfSystemCapabilities capabilities_for(const Config& config) {
  HbfSystemCapabilities capabilities;
  capabilities.spec_profile =
      config.simulation_profile != SimulationProfile::MediaResearch;
  capabilities.ai_system_semantics =
      config.simulation_profile == SimulationProfile::AiSystem;
  capabilities.transaction_protocol =
      config.protocol_abstraction == ProtocolAbstraction::Transaction;
  capabilities.research_stripe_mapping =
      config.research_stripe_mapping_enabled;
  capabilities.research_copy_gc = config.research_copy_gc_enabled;
  capabilities.research_migration_recovery =
      config.research_migration_recovery_enabled;
  return capabilities;
}

}  // namespace

std::string to_string(SimulationProfile profile) {
  switch (profile) {
    case SimulationProfile::MediaResearch: return "media_research";
    case SimulationProfile::HbfV07: return "hbf_v0_7";
    case SimulationProfile::AiSystem: return "ai_system";
  }
  return "unknown";
}

std::string to_string(ProtocolAbstraction abstraction) {
  switch (abstraction) {
    case ProtocolAbstraction::Transaction: return "transaction";
    case ProtocolAbstraction::Flit: return "flit";
  }
  return "unknown";
}

std::string to_string(ChannelMediaPolicy policy) {
  switch (policy) {
    case ChannelMediaPolicy::Linear: return "linear";
    case ChannelMediaPolicy::FineStripe: return "fine_stripe";
  }
  return "unknown";
}

std::string to_string(HbfCompletionClass completion) {
  switch (completion) {
    case HbfCompletionClass::Success: return "SUCCESS";
    case HbfCompletionClass::SuccessWithAdvisory:
      return "SUCCESS_WITH_ADVISORY";
    case HbfCompletionClass::RetryRequired: return "RETRY_REQUIRED";
    case HbfCompletionClass::Failed: return "FAILED";
  }
  return "UNKNOWN";
}

HbfCompletionClass hbf_completion_class(HbfStatus status) {
  switch (status) {
    case HbfStatus::Success:
      return HbfCompletionClass::Success;
    case HbfStatus::CorrectedEccRefreshRequired:
      return HbfCompletionClass::SuccessWithAdvisory;
    case HbfStatus::ProgramFailureReplayRequired:
    case HbfStatus::UncorrectableEccRefreshRequired:
    case HbfStatus::UncorrectableEccRetryRequired:
    case HbfStatus::TemporarilyRestricted:
    case HbfStatus::DieTemporarilyBlocked:
    case HbfStatus::ReadPendingWrite:
    case HbfStatus::MaxPendingDluReached:
    case HbfStatus::DluAccumulationTimeout:
      return HbfCompletionClass::RetryRequired;
    default:
      return HbfCompletionClass::Failed;
  }
}

bool hbf_data_valid(HbfStatus status) {
  const auto completion = hbf_completion_class(status);
  return completion == HbfCompletionClass::Success ||
         completion == HbfCompletionClass::SuccessWithAdvisory;
}

std::string to_string(HbfStatus status) {
  switch (status) {
    case HbfStatus::Success: return "SUCCESS";
    case HbfStatus::InvalidAddress: return "INVALID_ADDRESS";
    case HbfStatus::TemporarilyRestricted:
      return "TEMPORARILY_RESTRICTED";
    case HbfStatus::InvalidUserField: return "INVALID_USER_FIELD";
    case HbfStatus::OverlappingAddress: return "OVERLAPPING_ADDRESS";
    case HbfStatus::MaxPendingDluReached:
      return "MAX_PENDING_DLU_REACHED";
    case HbfStatus::DluAccumulationTimeout:
      return "DLU_ACCUMULATION_TIMEOUT";
    case HbfStatus::WriteOrderViolation: return "WRITE_ORDER_VIOLATION";
    case HbfStatus::ProgramFailure: return "PROGRAM_FAILURE";
    case HbfStatus::EraseFailure: return "ERASE_FAILURE";
    case HbfStatus::UncorrectableEcc: return "UNCORRECTABLE_ECC";
    case HbfStatus::ProgramFailureReplayRequired:
      return "PROGRAM_FAILURE_REPLAY_REQUIRED";
    case HbfStatus::CorrectedEccRefreshRequired:
      return "CORRECTED_ECC_REFRESH_REQUIRED";
    case HbfStatus::UncorrectableEccRefreshRequired:
      return "UNCORRECTABLE_ECC_REFRESH_REQUIRED";
    case HbfStatus::UncorrectableEccRetryRequired:
      return "UNCORRECTABLE_ECC_RETRY_REQUIRED";
    case HbfStatus::ErasedPageRead: return "ERASED_PAGE_READ";
    case HbfStatus::ReadPendingWrite: return "READ_PENDING_WRITE";
    case HbfStatus::ReducedCapacity: return "REDUCED_CAPACITY";
    case HbfStatus::DieTemporarilyBlocked:
      return "DIE_TEMPORARILY_BLOCKED";
    case HbfStatus::Pending: return "PENDING";
  }
  return "UNKNOWN";
}

std::uint8_t hbf_status_code(OpType op, HbfStatus status) {
  if (status == HbfStatus::Success) return 0;
  if (op == OpType::Write) {
    switch (status) {
      case HbfStatus::InvalidAddress: return 0x1;
      case HbfStatus::OverlappingAddress: return 0x2;
      case HbfStatus::InvalidUserField: return 0x3;
      case HbfStatus::MaxPendingDluReached: return 0x4;
      case HbfStatus::DluAccumulationTimeout: return 0x5;
      case HbfStatus::WriteOrderViolation: return 0x6;
      case HbfStatus::ProgramFailureReplayRequired: return 0x7;
      case HbfStatus::ReducedCapacity: return 0x8;
      case HbfStatus::DieTemporarilyBlocked: return 0x9;
      default: break;
    }
  } else if (op == OpType::Read) {
    switch (status) {
      case HbfStatus::InvalidAddress: return 0x1;
      case HbfStatus::TemporarilyRestricted: return 0x2;
      case HbfStatus::InvalidUserField: return 0x3;
      case HbfStatus::UncorrectableEccRefreshRequired: return 0x4;
      case HbfStatus::CorrectedEccRefreshRequired: return 0x5;
      case HbfStatus::UncorrectableEccRetryRequired: return 0x6;
      case HbfStatus::ErasedPageRead: return 0x7;
      case HbfStatus::ReducedCapacity: return 0x8;
      case HbfStatus::DieTemporarilyBlocked: return 0x9;
      case HbfStatus::ReadPendingWrite: return 0xA;
      default: break;
    }
  }
  throw std::invalid_argument(
      "status is not defined for this HBF command class");
}

HbfResponse HbfResponse::success(std::uint64_t request_id,
                                 SimTime completion_time,
                                 std::uint64_t bytes_completed) {
  return HbfResponse{request_id, HbfStatus::Success,
                     HbfCompletionClass::Success, std::nullopt,
                     completion_time, bytes_completed, std::nullopt};
}

HbfResponse HbfResponse::failure(std::uint64_t request_id,
                                 HbfStatus status,
                                 HbfErrorInfo error,
                                 SimTime completion_time,
                                 std::uint64_t bytes_completed) {
  if (status == HbfStatus::Success)
    throw std::invalid_argument(
        "HbfResponse::failure requires a non-success status");
  return HbfResponse{request_id, status, hbf_completion_class(status),
                     std::nullopt, completion_time, bytes_completed,
                     std::move(error)};
}

HbfSystem::HbfSystem(const Config& config)
    : profile_(config.simulation_profile),
      protocol_abstraction_(config.protocol_abstraction),
      capabilities_(capabilities_for(config)),
      topology_(config),
      channels_(config),
      frontend_(config, channels_),
      mapper_(config, channels_, topology_),
      host_router_(config, channels_),
      reliability_(config),
      media_(config, topology_),
      controller_(config, media_),
      host_gc_manager_(config),
      refresh_manager_(config) {}

}  // namespace hbfsim
