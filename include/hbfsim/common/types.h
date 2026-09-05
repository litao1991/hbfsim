#pragma once

#include <cstdint>
#include <string>

namespace hbfsim {

using SimTime = std::uint64_t;

enum class OpType { Read, Write, Erase, Refresh, Invalidate };
enum class MappingPolicy { Linear, FineStripe, BurstStripe, HostManaged };
enum class StripeScope { Device, Stack, Custom };
enum class TransactionSource {
  User,
  Mapping,
  Maintenance,
  Refresh,
  GarbageCollection,
  Recovery,
};
enum class HostGcVictimPolicy { InvalidRatio, Greedy };
enum class BlockState { Free, Open, Closed, Erasing, Bad };
enum class PageState { Erased, Reading, Programming, Valid, Invalid, Failed };
enum class StripeState {
  Free,
  Open,
  Sealed,
  Degraded,
  Migrating,
  Stale,
  Erasing,
  Bad,
};
enum class ReadErrorStatus { Clean, Corrected, Uncorrectable };
enum class InitializationMode { Empty, ImageLoaded, Preconditioned };
enum class HostLinkDirection { Command, HostToDevice, DeviceToHost };
enum class SimulationPhase { Initialize, Warmup, Measure, Drain };
enum class SimulationProfile { MediaResearch, HbfV07, AiSystem };
enum class ProtocolAbstraction { Transaction, Flit };
enum class ChannelMediaPolicy { Linear, FineStripe };
enum class HbfCompletionClass {
  Success,
  SuccessWithAdvisory,
  RetryRequired,
  Failed,
};
enum class HbfStatus {
  Success,
  InvalidAddress,
  TemporarilyRestricted,
  InvalidUserField,
  OverlappingAddress,
  MaxPendingDluReached,
  DluAccumulationTimeout,
  WriteOrderViolation,
  ProgramFailure,
  EraseFailure,
  UncorrectableEcc,
  ProgramFailureReplayRequired,
  CorrectedEccRefreshRequired,
  UncorrectableEccRefreshRequired,
  UncorrectableEccRetryRequired,
  ErasedPageRead,
  ReadPendingWrite,
  ReducedCapacity,
  DieTemporarilyBlocked,
  Pending,
};
enum class RetirementGranularity { Block, Bank, Die, Channel };

std::string to_string(OpType op);
std::string to_string(TransactionSource source);
std::string to_string(SimulationProfile profile);
std::string to_string(ProtocolAbstraction abstraction);
std::string to_string(ChannelMediaPolicy policy);
std::string to_string(HbfCompletionClass completion);
std::string to_string(HbfStatus status);
HbfCompletionClass hbf_completion_class(HbfStatus status);
bool hbf_data_valid(HbfStatus status);
std::uint8_t hbf_status_code(OpType op, HbfStatus status);
OpType parse_op(const std::string& value);
std::uint64_t parse_size(const std::string& value);
double parse_bandwidth_bytes_per_ns(const std::string& value);

}  // namespace hbfsim
