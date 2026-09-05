#include "hbfsim/core.h"

#include <algorithm>
#include <stdexcept>

namespace hbfsim {

void ResourceTracker::configure(
    std::uint32_t stacks, std::uint32_t dies_per_stack,
    std::uint32_t planes_per_die, std::uint32_t ports_per_stack,
    std::uint32_t host_channels_per_stack) {
  dies_per_stack_ = dies_per_stack;
  planes_per_die_ = planes_per_die;
  ports_per_stack_ = ports_per_stack;
  host_channels_per_stack_ = host_channels_per_stack;
  stacks_.resize(stacks);
  planes_.resize(static_cast<std::size_t>(stacks) * dies_per_stack *
                 planes_per_die);
  dies_.resize(static_cast<std::size_t>(stacks) * dies_per_stack);
  ports_.resize(static_cast<std::size_t>(stacks) * ports_per_stack);
  host_channels_.resize(static_cast<std::size_t>(stacks) *
                        host_channels_per_stack);
}

SimTime ResourceTracker::busy_until(const BusyCounter& counter,
                                    SimTime until) {
  if (until < counter.last_time)
    throw std::logic_error("resource snapshot moved backward");
  return counter.busy_ns +
         (counter.active == 0 ? 0 : until - counter.last_time);
}

void ResourceTracker::advance(BusyCounter& counter, int delta,
                              SimTime now) {
  if (now < counter.last_time)
    throw std::logic_error("resource transition moved backward");
  if (counter.active != 0) counter.busy_ns += now - counter.last_time;
  counter.last_time = now;
  if (delta < 0 && counter.active < static_cast<std::uint32_t>(-delta))
    throw std::logic_error("resource active count underflow");
  counter.active = static_cast<std::uint32_t>(
      static_cast<int64_t>(counter.active) + delta);
  counter.maximum = std::max(counter.maximum, counter.active);
}

void ResourceTracker::advance(StackCounter& counter, ResourceKind kind,
                              int delta, SimTime now) {
  if (now < counter.last_time)
    throw std::logic_error("stack resource transition moved backward");
  const auto elapsed = now - counter.last_time;
  if (counter.arrays != 0) counter.array_busy_ns += elapsed;
  if (counter.fabrics != 0) counter.fabric_busy_ns += elapsed;
  if (counter.hosts != 0) counter.host_busy_ns += elapsed;
  if (counter.arrays != 0 && counter.fabrics != 0)
    counter.overlap_ns += elapsed;
  counter.active_plane_area_ns +=
      static_cast<long double>(counter.arrays) * elapsed;
  counter.last_time = now;
  auto* active = kind == ResourceKind::Array
                     ? &counter.arrays
                     : kind == ResourceKind::Fabric ? &counter.fabrics
                                                    : &counter.hosts;
  if (delta < 0 && *active < static_cast<std::uint32_t>(-delta))
    throw std::logic_error("stack resource active count underflow");
  *active = static_cast<std::uint32_t>(
      static_cast<int64_t>(*active) + delta);
  counter.max_arrays = std::max(counter.max_arrays, counter.arrays);
}

void ResourceTracker::transition(ResourceKind kind, std::uint32_t stack,
                                 std::uint32_t local_index, int delta,
                                 SimTime now) {
  if (delta != 1 && delta != -1)
    throw std::invalid_argument("resource delta must be +/-1");
  advance(stacks_.at(stack), kind, delta, now);
  if (kind == ResourceKind::Array) {
    const auto plane = static_cast<std::size_t>(stack) * dies_per_stack_ *
                           planes_per_die_ +
                       local_index;
    const auto die = static_cast<std::size_t>(stack) * dies_per_stack_ +
                     local_index / planes_per_die_;
    advance(planes_.at(plane), delta, now);
    advance(dies_.at(die), delta, now);
  } else if (kind == ResourceKind::Fabric) {
    advance(ports_.at(static_cast<std::size_t>(stack) * ports_per_stack_ +
                      local_index % ports_per_stack_),
            delta, now);
  } else {
    advance(host_channels_.at(
                static_cast<std::size_t>(stack) * host_channels_per_stack_ +
                local_index % host_channels_per_stack_),
            delta, now);
  }
}

SimTime ResourceTracker::array_busy(std::uint32_t stack,
                                    SimTime until) const {
  const auto& value = stacks_.at(stack);
  return value.array_busy_ns +
         (value.arrays == 0 ? 0 : until - value.last_time);
}

SimTime ResourceTracker::fabric_busy(std::uint32_t stack,
                                     SimTime until) const {
  const auto& value = stacks_.at(stack);
  return value.fabric_busy_ns +
         (value.fabrics == 0 ? 0 : until - value.last_time);
}

SimTime ResourceTracker::host_busy(std::uint32_t stack,
                                   SimTime until) const {
  const auto& value = stacks_.at(stack);
  return value.host_busy_ns +
         (value.hosts == 0 ? 0 : until - value.last_time);
}

SimTime ResourceTracker::array_fabric_overlap(std::uint32_t stack,
                                              SimTime until) const {
  const auto& value = stacks_.at(stack);
  return value.overlap_ns +
         (value.arrays == 0 || value.fabrics == 0
              ? 0
              : until - value.last_time);
}

long double ResourceTracker::active_plane_area(std::uint32_t stack,
                                               SimTime until) const {
  const auto& value = stacks_.at(stack);
  return value.active_plane_area_ns +
         static_cast<long double>(value.arrays) *
             (until - value.last_time);
}

std::uint32_t ResourceTracker::max_active_planes(
    std::uint32_t stack) const {
  return stacks_.at(stack).max_arrays;
}

SimTime ResourceTracker::plane_busy(std::uint32_t index,
                                    SimTime until) const {
  return busy_until(planes_.at(index), until);
}

SimTime ResourceTracker::die_busy(std::uint32_t index,
                                  SimTime until) const {
  return busy_until(dies_.at(index), until);
}

SimTime ResourceTracker::port_busy(std::uint32_t index,
                                   SimTime until) const {
  return busy_until(ports_.at(index), until);
}

SimTime ResourceTracker::host_channel_busy(std::uint32_t index,
                                           SimTime until) const {
  return busy_until(host_channels_.at(index), until);
}

}  // namespace hbfsim
