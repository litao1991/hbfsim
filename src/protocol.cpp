#include "hbfsim/core.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hbfsim {
namespace {

std::uint64_t checked_capacity(const Config& config) {
  std::uint64_t capacity = config.stacks;
  const auto multiply = [&](std::uint64_t value) {
    if (value != 0 && capacity >
                          std::numeric_limits<std::uint64_t>::max() / value)
      throw std::overflow_error("HBF capacity exceeds address range");
    capacity *= value;
  };
  multiply(config.dies_per_stack);
  multiply(config.planes_per_die);
  multiply(config.blocks_per_plane);
  multiply(config.pages_per_block);
  multiply(config.page_size);
  return capacity;
}

std::uint32_t effective_channel_count(const Config& config) {
  if (config.hbf_channel_count != 0) return config.hbf_channel_count;
  const auto count = static_cast<std::uint64_t>(config.stacks) *
                     config.host_channels_per_stack;
  if (count > std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("HBF channel count exceeds address range");
  return static_cast<std::uint32_t>(count);
}

}  // namespace

HbfChannelDomain::HbfChannelDomain(const Config& config)
    : channel_count_(effective_channel_count(config)),
      total_capacity_(checked_capacity(config)),
      interleave_(config.hbf_channel_interleave),
      axi_ports_per_channel_(config.axi_ports_per_channel),
      axi_port_interleave_(config.axi_port_interleave) {
  if (channel_count_ == 0 || interleave_ == 0 ||
      axi_ports_per_channel_ == 0 || axi_port_interleave_ == 0)
    throw std::invalid_argument("HBF channel geometry must be non-zero");
  const auto stripes = total_capacity_ / interleave_ +
                       (total_capacity_ % interleave_ != 0 ? 1 : 0);
  const auto stripes_per_channel =
      (stripes + channel_count_ - 1) / channel_count_;
  if (stripes_per_channel >
      std::numeric_limits<std::uint64_t>::max() / interleave_)
    throw std::overflow_error("HBF channel capacity exceeds address range");
  channel_capacity_ = stripes_per_channel * interleave_;
}

HbfChannelAddress HbfChannelDomain::translate(
    std::uint64_t global) const {
  if (global >= total_capacity_)
    throw std::out_of_range("global address exceeds HBF capacity");
  const auto stripe = global / interleave_;
  const auto offset = global % interleave_;
  HbfChannelAddress address;
  address.channel = static_cast<std::uint32_t>(stripe % channel_count_);
  address.local_address = (stripe / channel_count_) * interleave_ + offset;
  const auto port_stripe = address.local_address / axi_port_interleave_;
  const auto port_offset = address.local_address % axi_port_interleave_;
  address.axi_port =
      static_cast<std::uint32_t>(port_stripe % axi_ports_per_channel_);
  address.axi_port_local_address =
      (port_stripe / axi_ports_per_channel_) * axi_port_interleave_ +
      port_offset;
  return address;
}

std::uint64_t HbfChannelDomain::global_address(
    const HbfChannelAddress& address) const {
  if (address.channel >= channel_count_)
    throw std::out_of_range("HBF channel index exceeds channel count");
  const auto local_stripe = address.local_address / interleave_;
  const auto offset = address.local_address % interleave_;
  if (local_stripe >
      (std::numeric_limits<std::uint64_t>::max() - address.channel) /
          channel_count_)
    throw std::out_of_range("HBF local address exceeds address range");
  const auto global_stripe = local_stripe * channel_count_ + address.channel;
  if (global_stripe >
      (std::numeric_limits<std::uint64_t>::max() - offset) / interleave_)
    throw std::out_of_range("HBF local address exceeds address range");
  const auto global = global_stripe * interleave_ + offset;
  if (global >= total_capacity_)
    throw std::out_of_range("HBF local address is outside device capacity");
  return global;
}

HbfValidationResult HbfProtocolValidator::validate(
    const TraceEntry& entry) const {
  HbfValidationResult result;
  try {
    result.address = channels_.translate(entry.address);
  } catch (const std::out_of_range& error) {
    return {HbfStatus::InvalidAddress, error.what(), std::nullopt};
  }

  if (entry.size != 0 &&
      entry.size - 1 > channels_.total_capacity() - 1 - entry.address)
    return {HbfStatus::InvalidAddress,
            "request range exceeds HBF capacity", result.address};

  if (entry.op != OpType::Read && entry.op != OpType::Write)
    return result;
  if (entry.size == 0 || entry.address % 64 != 0 || entry.size % 64 != 0)
    return {HbfStatus::InvalidUserField,
            "HBF Read/Write transfers must be non-empty and 64B aligned",
            result.address};
  if (entry.size > config_.dlu_size)
    return {HbfStatus::InvalidUserField,
            "one HBF Read/Write command cannot exceed one 4KiB DLU",
            result.address};

  const auto last = channels_.translate(entry.address + entry.size - 1);
  if (last.channel != result.address->channel ||
      last.axi_port != result.address->axi_port)
    return {HbfStatus::InvalidAddress,
            "request crosses an HBF Channel or AXI Port range",
            result.address};
  if (result.address->local_address / config_.dlu_size !=
      last.local_address / config_.dlu_size)
    return {HbfStatus::InvalidAddress,
            "request crosses a Channel-local NAND Page/DLU boundary",
            result.address};
  if (entry.axi_port != std::numeric_limits<std::uint32_t>::max() &&
      entry.axi_port != result.address->axi_port)
    return {HbfStatus::InvalidAddress,
            "address is outside the selected AXI Port range",
            result.address};
  return result;
}

std::size_t AxiOrderTracker::EndpointHash::operator()(
    const EndpointKey& key) const {
  std::size_t result = key.channel;
  result = result * 1315423911U + key.port;
  return result * 1315423911U + key.id;
}

AxiOrderTracker::AxiOrderTracker(const Config& config)
    : channel_count_(effective_channel_count(config)),
      ports_per_channel_(config.axi_ports_per_channel),
      id_count_(config.axi_id_count),
      max_outstanding_per_id_(config.axi_max_outstanding_per_id) {}

HbfStatus AxiOrderTracker::issue(const AxiEndpoint& endpoint,
                                 std::uint64_t request_id) {
  if (endpoint.channel >= channel_count_ ||
      endpoint.port >= ports_per_channel_ || endpoint.id >= id_count_)
    return HbfStatus::InvalidUserField;
  if (owners_.contains(request_id)) return HbfStatus::InvalidUserField;
  const EndpointKey key{endpoint.channel, endpoint.port, endpoint.id};
  auto& queue = issued_[key];
  if (queue.size() >= max_outstanding_per_id_)
    return HbfStatus::TemporarilyRestricted;
  queue.push_back(request_id);
  owners_.emplace(request_id, key);
  return HbfStatus::Success;
}

std::vector<HbfResponse> AxiOrderTracker::complete(HbfResponse response) {
  const auto owner = owners_.find(response.request_id);
  if (owner == owners_.end())
    throw std::logic_error("AXI completion has no matching issued request");
  const auto key = owner->second;
  const auto [_, inserted] =
      completed_.emplace(response.request_id, std::move(response));
  if (!inserted) throw std::logic_error("duplicate AXI completion");

  auto queue_it = issued_.find(key);
  if (queue_it == issued_.end())
    throw std::logic_error("AXI endpoint queue is missing");
  auto& queue = queue_it->second;
  std::vector<HbfResponse> released;
  while (!queue.empty()) {
    const auto complete = completed_.find(queue.front());
    if (complete == completed_.end()) break;
    released.push_back(std::move(complete->second));
    completed_.erase(complete);
    owners_.erase(queue.front());
    queue.pop_front();
  }
  if (queue.empty()) issued_.erase(queue_it);
  return released;
}

std::size_t AxiOrderTracker::outstanding(
    const AxiEndpoint& endpoint) const {
  const EndpointKey key{endpoint.channel, endpoint.port, endpoint.id};
  const auto it = issued_.find(key);
  return it == issued_.end() ? 0 : it->second.size();
}

std::size_t DluAssembler::DluKeyHash::operator()(const DluKey& key) const {
  return std::hash<std::uint64_t>{}(key.local_base) ^
         (static_cast<std::size_t>(key.channel) << 1);
}

DluAssembler::DluAssembler(const Config& config)
    : dlu_size_(config.dlu_size),
      max_pending_dlus_(config.max_pending_dlus),
      timeout_ns_(config.dlu_accumulation_timeout_ns) {
  if (dlu_size_ == 0 || dlu_size_ % 64 != 0 ||
      max_pending_dlus_ == 0 || timeout_ns_ == 0)
    throw std::invalid_argument("DLU geometry and timeout must be non-zero");
}

DluAssemblyResult DluAssembler::submit(
    std::uint64_t request_id, const HbfChannelAddress& address,
    std::uint64_t bytes, SimTime now,
    std::optional<SimTime> data_ready_at, SimTime h2d_wait_ns,
    SimTime h2d_service_ns) {
  const auto offset = address.local_address % dlu_size_;
  if (bytes == 0 || bytes > dlu_size_ - offset || offset % 64 != 0 ||
      bytes % 64 != 0)
    return {HbfStatus::InvalidUserField, std::nullopt, std::nullopt};
  const DluKey key{address.channel, address.local_address - offset};
  auto pending = pending_.find(key);
  bool created = false;
  if (pending == pending_.end()) {
    if (pending_per_channel_[address.channel] >= max_pending_dlus_)
      return {HbfStatus::MaxPendingDluReached, std::nullopt, std::nullopt};
    const auto first_arrival = data_ready_at.value_or(now);
    const auto deadline =
        first_arrival > std::numeric_limits<SimTime>::max() - timeout_ns_
            ? std::numeric_limits<SimTime>::max()
            : first_arrival + timeout_ns_;
    PendingDlu value;
    value.generation = next_generation_++;
    value.deadline = deadline;
    const auto fragment_count = dlu_size_ / 64;
    value.coverage.resize((fragment_count + 63) / 64, 0);
    value.fragment_ready_at.resize(fragment_count, 0);
    pending = pending_.emplace(key, std::move(value)).first;
    deadlines_.push({deadline, key, pending->second.generation});
    ++pending_per_channel_[address.channel];
    created = true;
  }

  auto& value = pending->second;
  const auto first_fragment = offset / 64;
  const auto fragment_count = bytes / 64;
  for (std::uint64_t fragment = first_fragment;
       fragment < first_fragment + fragment_count; ++fragment) {
    if ((value.coverage.at(fragment / 64) &
         (1ULL << (fragment % 64))) != 0)
      return {HbfStatus::OverlappingAddress, std::nullopt, value.deadline};
  }
  for (std::uint64_t fragment = first_fragment;
       fragment < first_fragment + fragment_count; ++fragment) {
    value.coverage.at(fragment / 64) |= 1ULL << (fragment % 64);
    value.fragment_ready_at.at(fragment) = data_ready_at.value_or(now);
  }
  value.covered_bytes += bytes;
  value.request_ids.push_back(request_id);
  const auto arrival = data_ready_at.value_or(now);
  if (value.timing.fragment_count == 0)
    value.timing.first_fragment_arrival = arrival;
  value.timing.last_fragment_arrival =
      std::max(value.timing.last_fragment_arrival, arrival);
  value.timing.total_h2d_wait_ns += h2d_wait_ns;
  value.timing.total_h2d_service_ns += h2d_service_ns;
  value.timing.fragment_count += static_cast<std::uint32_t>(fragment_count);
  if (value.covered_bytes != dlu_size_)
    return {HbfStatus::Pending, std::nullopt,
            created ? std::optional<SimTime>(value.deadline) : std::nullopt};

  const auto latest_arrival = *std::max_element(
      value.fragment_ready_at.begin(), value.fragment_ready_at.end());
  if (latest_arrival > value.deadline)
    return {HbfStatus::Pending, std::nullopt, std::nullopt};

  HbfDlu completed{{key.channel, key.local_base}, dlu_size_,
                   std::move(value.request_ids), value.timing};
  pending_.erase(pending);
  --pending_per_channel_.at(key.channel);
  return {HbfStatus::Success, std::move(completed), std::nullopt};
}

std::vector<ExpiredDlu> DluAssembler::expire(SimTime now) {
  std::vector<ExpiredDlu> expired;
  while (!deadlines_.empty() && deadlines_.top().deadline <= now) {
    const auto entry = deadlines_.top();
    deadlines_.pop();
    auto it = pending_.find(entry.key);
    if (it == pending_.end() ||
        it->second.generation != entry.generation ||
        it->second.deadline != entry.deadline)
      continue;
    expired.push_back({{entry.key.channel, entry.key.local_base},
                       std::move(it->second.request_ids),
                       HbfStatus::DluAccumulationTimeout,
                       it->second.timing});
    const auto channel = entry.key.channel;
    pending_.erase(it);
    --pending_per_channel_.at(channel);
  }
  return expired;
}

DluReadResult DluAssembler::lookup(const HbfChannelAddress& address,
                                   std::uint64_t bytes) const {
  const auto offset = address.local_address % dlu_size_;
  if (bytes == 0 || bytes > dlu_size_ - offset || offset % 64 != 0 ||
      bytes % 64 != 0)
    return {DluReadDisposition::NotPending, HbfStatus::InvalidUserField, 0};
  const DluKey key{address.channel, address.local_address - offset};
  const auto pending = pending_.find(key);
  if (pending == pending_.end())
    return {DluReadDisposition::NotPending, HbfStatus::Success, 0};
  const auto first_fragment = offset / 64;
  const auto fragment_count = bytes / 64;
  SimTime ready_at = 0;
  for (std::uint64_t fragment = first_fragment;
       fragment < first_fragment + fragment_count; ++fragment) {
    if ((pending->second.coverage.at(fragment / 64) &
         (1ULL << (fragment % 64))) == 0)
      return {DluReadDisposition::PendingWrite,
              HbfStatus::ReadPendingWrite, 0};
    ready_at = std::max(ready_at,
                        pending->second.fragment_ready_at.at(fragment));
  }
  return {DluReadDisposition::Forwarded, HbfStatus::Success, ready_at};
}

}  // namespace hbfsim
