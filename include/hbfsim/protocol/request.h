#pragma once

#include "hbfsim/common/address.h"
#include "hbfsim/common/types.h"
#include "hbfsim/protocol/axi.h"
#include "hbfsim/protocol/status.h"
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace hbfsim {

struct HostRoute {
  std::uint32_t stack = 0;
  std::uint32_t channel = 0;
  std::uint32_t global_channel = 0;
  std::uint64_t channel_local_address = 0;
  std::uint32_t axi_port = 0;
  std::uint64_t axi_port_local_address = 0;
};

struct Request {
  std::uint64_t id = 0;
  SimTime arrival_time = 0;
  OpType op = OpType::Read;
  ReadType read_type = ReadType::Single;
  std::uint64_t logical_addr = 0;
  std::uint64_t size = 0;
  std::uint32_t stream_id = 0;
  SimTime dlu_data_ready = 0;
  std::vector<std::uint64_t> dlu_request_ids;
  HostRoute host_route;
  std::uint32_t pending_subreqs = 0;
  SimTime complete_time = 0;
  SimTime host_command_wait_ns = 0;
  SimTime host_command_service_ns = 0;
  bool measured = true;
  bool failed = false;
  bool internal = false;
  bool axi_tracked = false;
  HbfStatus status = HbfStatus::Success;
  AxiEndpoint axi;
  TransactionSource source = TransactionSource::User;
};

struct LatencyBreakdown {
  SimTime host_command_wait_ns = 0;
  SimTime host_command_service_ns = 0;
  SimTime host_data_wait_ns = 0;
  SimTime host_data_service_ns = 0;
  SimTime nand_queue_wait_ns = 0;
  SimTime nand_command_wait_ns = 0;
  SimTime array_service_ns = 0;
  SimTime auto_erase_service_ns = 0;
  SimTime fabric_wait_ns = 0;
  SimTime fabric_service_ns = 0;
};

struct SubRequest {
  std::uint64_t id = 0;
  std::uint64_t parent_id = 0;
  OpType op = OpType::Read;
  ReadType read_type = ReadType::Single;
  TransactionSource source = TransactionSource::User;
  std::uint64_t lpn = 0;
  std::uint64_t bytes = 0;
  PhysicalAddr paddr;
  std::optional<PhysicalAddr> old_paddr;
  std::optional<std::uint64_t> copy_job_id;
  std::optional<std::uint32_t> copy_slot;
  SimTime arrival_time = 0;
  SimTime enqueue_time = 0;
  SimTime issue_time = 0;
  SimTime complete_time = 0;
  SimTime ready_time = 0;
  SimTime array_active_since = 0;
  SimTime array_completion_time = 0;
  SimTime suspended_remaining_ns = 0;
  std::uint32_t read_attempts = 0;
  HostRoute host_route;
  LatencyBreakdown latency;
  bool suspended = false;
  bool failed = false;
  bool critical = false;
  bool page0_auto_erase = false;
  bool auto_erase_failed = false;
  bool auto_erase_retired = false;
  bool batch_released = false;
  bool batch_sense_held = false;
  HbfStatus status = HbfStatus::Success;
};

using FlashTransaction = SubRequest;

struct TraceEntry {
  SimTime timestamp_ns;
  OpType op;
  std::uint64_t address;
  std::uint64_t size;
  std::uint32_t stream;
  std::uint32_t axi_id = 0;
  std::uint32_t axi_port = std::numeric_limits<std::uint32_t>::max();
  bool batch_hint = false;
};

}  // namespace hbfsim
