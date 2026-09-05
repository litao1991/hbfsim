#include "hbfsim/controller/base_die.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace hbfsim {
namespace {

std::size_t source_index(TransactionSource source) {
  return static_cast<std::size_t>(source);
}

PlaneControllerState::SourceQueues& queues_for(PlaneControllerState& plane,
                                                OpType op) {
  if (op == OpType::Read) return plane.reads;
  if (op == OpType::Write) return plane.writes;
  if (op == OpType::Erase) return plane.erases;
  return plane.refreshes;
}

const char* queue_name(OpType op) {
  if (op == OpType::Read) return "read";
  if (op == OpType::Write) return "write";
  if (op == OpType::Erase) return "erase";
  return "refresh";
}

}  // namespace

ControllerExecutionState::ControllerExecutionState(
    const Config& config, const NandTopology& topology)
    : active_per_die(topology.die_count(), 0),
      active_per_stack(config.stacks, 0),
      dispatch_cursor_per_stack(config.stacks, 0),
      dispatch_wake_at(config.stacks, std::numeric_limits<SimTime>::max()),
      program_ready(config.stacks) {}

BaseDieController::BaseDieController(const Config& config,
                                     NandMediaSystem& media)
    : config_(config), media_(media), scheduler_(config),
      interconnect_(config), execution_(config, media.topology()),
      plane_states_(media.topology().plane_count()) {}

PlaneControllerState& BaseDieController::plane_state(
    const PhysicalAddr& address) {
  return plane_states_.at(media_.topology().flat_plane(address));
}

const PlaneControllerState& BaseDieController::plane_state(
    const PhysicalAddr& address) const {
  return plane_states_.at(media_.topology().flat_plane(address));
}

int SchedulingPolicy::base_priority(const SubRequest& request) const {
  if ((request.source == TransactionSource::Recovery ||
       request.source == TransactionSource::HostReplay) && request.critical)
    return 0;
  if (request.source == TransactionSource::User &&
      request.op == OpType::Read)
    return 1;
  if (request.source == TransactionSource::Recovery ||
      request.source == TransactionSource::HostReplay) return 2;
  if (request.source == TransactionSource::User ||
      request.source == TransactionSource::Mapping)
    return 3;
  if (request.source == TransactionSource::Maintenance ||
      request.source == TransactionSource::Refresh ||
      request.source == TransactionSource::HostRefresh)
    return 4;
  return 5;
}

int SchedulingPolicy::priority(const SubRequest& request, SimTime waited,
                               const PlaneControllerState& plane) const {
  auto value = base_priority(request);
  if (waited >= config_.source_aging_ns ||
      (request.op != OpType::Read &&
       ((request.source != TransactionSource::GarbageCollection &&
         waited >= config_.write_starvation_ns) ||
        plane.consecutive_reads >= config_.max_consecutive_reads)))
    value = 0;
  return value;
}

void MediaScheduler::enqueue(PlaneControllerState& plane,
                             const SubRequest& request) const {
  queues_for(plane, request.op)
      .at(source_index(request.source))
      .push_back(request.id);
}

void MediaScheduler::dequeue(PlaneControllerState& plane,
                             const SubRequest& request,
                             bool update_read_streak) const {
  auto& queue =
      queues_for(plane, request.op).at(source_index(request.source));
  if (queue.empty() || queue.front() != request.id)
    throw std::logic_error(std::string(queue_name(request.op)) +
                           " queue invariant violated");
  queue.pop_front();
  if (!update_read_streak) return;
  if (request.op == OpType::Read)
    ++plane.consecutive_reads;
  else
    plane.consecutive_reads = 0;
}

std::optional<std::uint64_t> MediaScheduler::choose(
    const PlaneControllerState& plane, SimTime now,
    const Lookup& lookup) const {
  std::optional<std::uint64_t> selected;
  int selected_priority = 0;
  const auto consider = [&](const PlaneControllerState::SourceQueues& queues) {
    for (const auto& queue : queues) {
      if (queue.empty()) continue;
      const auto id = queue.front();
      const auto& candidate = lookup(id);
      const auto waited = now - candidate.enqueue_time;
      const auto candidate_priority = policy_.priority(candidate, waited, plane);
      if (!selected || candidate_priority < selected_priority ||
          (candidate_priority == selected_priority &&
           (candidate.enqueue_time < lookup(*selected).enqueue_time ||
            (candidate.enqueue_time == lookup(*selected).enqueue_time &&
             candidate.id < *selected)))) {
        selected = id;
        selected_priority = candidate_priority;
      }
    }
  };
  consider(plane.reads);
  consider(plane.writes);
  consider(plane.erases);
  consider(plane.refreshes);
  return selected;
}

bool MediaScheduler::queues_empty(
    const PlaneControllerState::SourceQueues& queues) const {
  return std::all_of(queues.begin(), queues.end(),
                     [](const auto& queue) { return queue.empty(); });
}

std::size_t MediaScheduler::depth_index(OpType op) {
  if (op == OpType::Read) return 0;
  if (op == OpType::Write) return 1;
  if (op == OpType::Erase) return 2;
  return 3;
}

InterconnectModel::InterconnectModel(const Config& config) {
  host_interfaces_.reserve(config.stacks);
  fabrics_.reserve(config.stacks);
  for (std::uint32_t stack = 0; stack < config.stacks; ++stack) {
    host_interfaces_.emplace_back(
        config.host_channels_per_stack, config.host_bw_bytes_per_ns,
        config.host_fixed_latency_ns, config.host_full_duplex);
    fabrics_.emplace_back(
        config.ports_per_stack, config.internal_bw_bytes_per_ns,
        config.internal_port_bw_bytes_per_ns,
        config.internal_fixed_latency_ns);
  }
}

LinkResource::Reservation InterconnectModel::reserve_host(
    const HostRoute& route, HostLinkDirection direction, SimTime now,
    std::uint64_t bytes) {
  return host_interfaces_.at(route.stack).reserve(route, direction, now,
                                                  bytes);
}

LinkResource::Reservation InterconnectModel::reserve_fabric(
    const PhysicalAddr& address, SimTime now, std::uint64_t bytes) {
  return fabrics_.at(address.stack).reserve_window(now, bytes,
                                                   address.data_port);
}

SimTime BaseDieController::command_ready_time(
    const SubRequest& request) const {
  const auto& target = media_.plane(request.paddr);
  const auto& block = target.blocks.at(request.paddr.block);
  const auto& die = media_.die(request.paddr);
  const auto command_ready =
      config_.simulation_profile == SimulationProfile::MediaResearch
          ? die.command_ready_at
          : media_.bank(request.paddr).command_ready_at;
  return std::max({request.ready_time, target.ready_at, block.ready_at,
                   die.ready_at, command_ready});
}

void BaseDieController::claim_command(const SubRequest& request,
                                      SimTime now, bool shared_command) {
  media_.claim_command_ready(request.paddr, now + config_.t_ccs_ns);
  if (shared_command) return;
  if (config_.simulation_profile == SimulationProfile::MediaResearch) {
    auto& die = media_.die(request.paddr);
    die.command_ready_at =
        std::max(die.command_ready_at, now + config_.t_ccs_ns);
  } else {
    auto& bank = media_.bank(request.paddr);
    bank.command_ready_at =
        std::max(bank.command_ready_at, now + config_.t_ccs_ns);
  }
}

}  // namespace hbfsim
