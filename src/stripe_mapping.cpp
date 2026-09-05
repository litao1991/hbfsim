#include "hbfsim/core.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hbfsim {
namespace {

std::string stripe_name(const StripeId& stripe) {
  return std::to_string(stripe.physical_id) + ":" +
         std::to_string(stripe.generation);
}

}  // namespace

bool LazyBitmap::test(std::uint32_t bit) const {
  const auto word = bit / 64;
  return word < words_.size() &&
         (words_[word] & (std::uint64_t{1} << (bit % 64))) != 0;
}

void LazyBitmap::set(std::uint32_t bit, std::uint32_t bit_count) {
  if (bit >= bit_count) throw std::out_of_range("bitmap bit out of range");
  if (words_.empty()) words_.resize((bit_count + 63) / 64, 0);
  words_.at(bit / 64) |= std::uint64_t{1} << (bit % 64);
}

void LazyBitmap::clear(std::uint32_t bit) {
  const auto word = bit / 64;
  if (word < words_.size())
    words_[word] &= ~(std::uint64_t{1} << (bit % 64));
}

bool LazyBitmap::any() const {
  return std::any_of(words_.begin(), words_.end(),
                     [](std::uint64_t word) { return word != 0; });
}

std::uint32_t LazyBitmap::count() const {
  std::uint32_t result = 0;
  for (const auto word : words_)
    result += static_cast<std::uint32_t>(std::popcount(word));
  return result;
}

StripeMappingTable::StripeMappingTable(const Config& config)
    : config_(config) {
  const auto width = static_cast<std::uint64_t>(config_.stacks) *
                     config_.dies_per_stack * config_.planes_per_die;
  const auto capacity = width * config_.pages_per_block;
  if (width > std::numeric_limits<std::uint32_t>::max() ||
      capacity > std::numeric_limits<std::uint32_t>::max())
    throw std::runtime_error("host-managed stripe geometry exceeds 32-bit slots");
  stripe_width_ = static_cast<std::uint32_t>(width);
  stripe_capacity_ = static_cast<std::uint32_t>(capacity);
  auto scaled_reserved =
      static_cast<long double>(config_.blocks_per_plane) *
      config_.host_gc_overprovisioning_ratio;
  const auto nearest_reserved = std::round(scaled_reserved);
  const auto tolerance =
      4.0L * std::numeric_limits<double>::epsilon() *
      std::max(1.0L, std::abs(scaled_reserved));
  if (std::abs(scaled_reserved - nearest_reserved) <= tolerance)
    scaled_reserved = nearest_reserved;
  const auto reserved = std::min<std::size_t>(
      config_.blocks_per_plane - 1,
      static_cast<std::size_t>(std::ceil(scaled_reserved)));
  host_visible_stripes_ = config_.blocks_per_plane - reserved;
  descriptors_.resize(config_.blocks_per_plane);
  generations_.resize(config_.blocks_per_plane, 0);
  for (std::uint64_t physical = 0; physical < config_.blocks_per_plane;
       ++physical) {
    descriptors_[physical].id.physical_id = physical;
    free_stripes_.push_back(physical);
  }
}

std::uint64_t StripeMappingTable::logical_base(std::uint64_t lpn) const {
  if (lpn / stripe_capacity_ >= host_visible_stripes_)
    throw std::out_of_range(
        "logical page exceeds Host-visible HBF capacity");
  return (lpn / stripe_capacity_) * stripe_capacity_;
}

std::vector<StripeId> StripeMappingTable::active_stripes() const {
  std::vector<StripeId> result;
  result.reserve(active_.size());
  for (const auto& [_, stripe] : active_) result.push_back(stripe);
  return result;
}

StripeId StripeMappingTable::allocate_internal(std::uint64_t base,
                                               bool publish) {
  if (base % stripe_capacity_ != 0)
    throw std::invalid_argument("STRIPE_LPN_NOT_ALIGNED");
  if (base / stripe_capacity_ >= host_visible_stripes_)
    throw std::out_of_range(
        "logical stripe exceeds Host-visible HBF capacity");
  if (publish && active_.contains(base))
    throw std::runtime_error("LOGICAL_STRIPE_ALREADY_ALLOCATED");
  if (free_stripes_.empty())
    throw std::runtime_error("NO_FREE_PHYSICAL_STRIPE");

  const auto physical = free_stripes_.front();
  free_stripes_.pop_front();
  auto& generation = generations_.at(physical);
  if (generation == std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("STRIPE_GENERATION_EXHAUSTED");
  ++generation;

  StripeDescriptor fresh;
  fresh.id = {physical, generation};
  fresh.logical_base_lpn = base;
  fresh.state = StripeState::Open;
  descriptors_.at(physical) = std::move(fresh);
  if (publish) active_[base] = descriptors_.at(physical).id;
  return descriptors_.at(physical).id;
}

StripeId StripeMappingTable::allocate(std::uint64_t base) {
  return allocate_internal(base, true);
}

StripeId StripeMappingTable::allocate_replacement(std::uint64_t base) {
  if (!active_.contains(base))
    throw std::runtime_error("REPLACEMENT_WITHOUT_ACTIVE_SOURCE");
  return allocate_internal(base, false);
}

const StripeDescriptor& StripeMappingTable::descriptor(
    const StripeId& stripe) const {
  if (stripe.physical_id >= descriptors_.size())
    throw std::out_of_range("unknown physical stripe");
  const auto& result = descriptors_.at(stripe.physical_id);
  if (result.id.generation != stripe.generation || !stripe.valid())
    throw std::runtime_error("STALE_GENERATION: " + stripe_name(stripe));
  return result;
}

StripeDescriptor& StripeMappingTable::mutable_descriptor(
    const StripeId& stripe) {
  return const_cast<StripeDescriptor&>(
      std::as_const(*this).descriptor(stripe));
}

PhysicalAddr StripeMappingTable::address_for(const StripeId& stripe,
                                             std::uint32_t slot) const {
  descriptor(stripe);
  if (slot >= stripe_capacity_)
    throw std::out_of_range("stripe slot out of range");
  const auto lane = slot % stripe_width_;
  const auto row = slot / stripe_width_;
  const auto planes_per_stack = static_cast<std::uint64_t>(
      config_.dies_per_stack) * config_.planes_per_die;

  PhysicalAddr result;
  result.stack = static_cast<std::uint32_t>(lane / planes_per_stack);
  const auto local_lane = lane % planes_per_stack;
  result.die = static_cast<std::uint32_t>(local_lane /
                                          config_.planes_per_die);
  result.plane = static_cast<std::uint32_t>(local_lane %
                                            config_.planes_per_die);
  result.block = static_cast<std::uint32_t>(stripe.physical_id);
  result.page = row;
  result.data_port = lane % config_.ports_per_stack;
  result.physical_stripe = stripe.physical_id;
  result.generation = stripe.generation;
  return result;
}

std::uint32_t StripeMappingTable::slot_of(const PhysicalAddr& paddr) const {
  if (paddr.stack >= config_.stacks ||
      paddr.die >= config_.dies_per_stack ||
      paddr.plane >= config_.planes_per_die ||
      paddr.block >= config_.blocks_per_plane ||
      paddr.page >= config_.pages_per_block)
    throw std::out_of_range("physical address outside stripe geometry");
  const auto lane =
      (paddr.stack * config_.dies_per_stack + paddr.die) *
          config_.planes_per_die +
      paddr.plane;
  return paddr.page * stripe_width_ + lane;
}

PhysicalAddr StripeMappingTable::preview_for(const StripeId& stripe,
                                             std::uint64_t lpn) const {
  const auto& target = descriptor(stripe);
  if (lpn < target.logical_base_lpn ||
      lpn >= target.logical_base_lpn + stripe_capacity_)
    throw std::runtime_error("LPN_OUTSIDE_STRIPE");
  const auto slot = static_cast<std::uint32_t>(lpn -
                                                target.logical_base_lpn);
  if (slot != target.next_program_slot)
    throw std::runtime_error("STRIPE_WRITE_ORDER_VIOLATION");
  return address_for(stripe, slot);
}

PhysicalAddr StripeMappingTable::preview_program(std::uint64_t lpn) const {
  const auto base = logical_base(lpn);
  if (const auto it = active_.find(base); it != active_.end())
    return preview_for(it->second, lpn);
  if (lpn != base) throw std::runtime_error("STRIPE_WRITE_ORDER_VIOLATION");
  if (free_stripes_.empty()) throw std::runtime_error("NO_FREE_PHYSICAL_STRIPE");
  const auto physical = free_stripes_.front();
  if (generations_.at(physical) ==
      std::numeric_limits<std::uint32_t>::max())
    throw std::overflow_error("STRIPE_GENERATION_EXHAUSTED");
  const auto next_generation = generations_.at(physical) + 1;
  const auto lane = std::uint32_t{0};
  PhysicalAddr result;
  result.block = static_cast<std::uint32_t>(physical);
  result.data_port = lane % config_.ports_per_stack;
  result.physical_stripe = physical;
  result.generation = next_generation;
  return result;
}

PhysicalAddr StripeMappingTable::reserve_program(std::uint64_t lpn) {
  const auto base = logical_base(lpn);
  auto it = active_.find(base);
  if (it == active_.end()) {
    if (lpn != base) throw std::runtime_error("STRIPE_WRITE_ORDER_VIOLATION");
    const auto stripe = allocate(base);
    it = active_.find(base);
    if (it == active_.end() || it->second != stripe)
      throw std::logic_error("stripe allocation was not published");
  }
  return reserve_program(it->second, lpn);
}

PhysicalAddr StripeMappingTable::reserve_program(
    const StripeId& destination, std::uint64_t lpn) {
  auto& target = mutable_descriptor(destination);
  if (target.state != StripeState::Open)
    throw std::runtime_error("PROGRAM_REQUIRES_OPEN_STRIPE");
  const auto address = preview_for(destination, lpn);
  const auto slot = target.next_program_slot;
  if (target.valid_bitmap.test(slot) || target.invalid_bitmap.test(slot) ||
      target.failed_bitmap.test(slot) || target.hole_bitmap.test(slot) ||
      target.reserved_bitmap.test(slot))
    throw std::logic_error("STRIPE_SLOT_ALREADY_CONSUMED");
  target.reserved_bitmap.set(slot, stripe_capacity_);
  ++target.reserved_programs;
  ++target.next_program_slot;
  return address;
}

void StripeMappingTable::reserve_hole(const StripeId& destination,
                                      std::uint64_t lpn) {
  auto& target = mutable_descriptor(destination);
  if (target.state != StripeState::Open)
    throw std::runtime_error("HOLE_REQUIRES_OPEN_STRIPE");
  preview_for(destination, lpn);
  const auto slot = target.next_program_slot;
  target.hole_bitmap.set(slot, stripe_capacity_);
  ++target.next_program_slot;
}

void StripeMappingTable::commit_program(std::uint64_t lpn,
                                        const PhysicalAddr& paddr) {
  const StripeId stripe{paddr.physical_stripe, paddr.generation};
  auto& target = mutable_descriptor(stripe);
  const auto slot = slot_of(paddr);
  if (lpn != target.logical_base_lpn + slot)
    throw std::runtime_error("PROGRAM_LPN_PPA_MISMATCH");
  if (!target.reserved_bitmap.test(slot))
    throw std::runtime_error("PROGRAM_SLOT_NOT_RESERVED");
  target.reserved_bitmap.clear(slot);
  --target.reserved_programs;
  target.valid_bitmap.set(slot, stripe_capacity_);
  target.invalid_bitmap.clear(slot);
  target.failed_bitmap.clear(slot);
  ++target.valid_slots;
  if (target.next_program_slot == stripe_capacity_ &&
      target.reserved_programs == 0)
    target.state = target.failed_bitmap.any() ? StripeState::Degraded
                                               : StripeState::Sealed;
}

ProgramFailureNotice StripeMappingTable::fail_program(
    std::uint64_t lpn, const PhysicalAddr& paddr) {
  const StripeId stripe{paddr.physical_stripe, paddr.generation};
  auto& target = mutable_descriptor(stripe);
  const auto slot = slot_of(paddr);
  if (lpn != target.logical_base_lpn + slot)
    throw std::runtime_error("PROGRAM_LPN_PPA_MISMATCH");
  if (!target.reserved_bitmap.test(slot))
    throw std::runtime_error("PROGRAM_SLOT_NOT_RESERVED");
  target.reserved_bitmap.clear(slot);
  --target.reserved_programs;
  target.failed_bitmap.set(slot, stripe_capacity_);
  target.valid_bitmap.clear(slot);
  target.invalid_bitmap.clear(slot);
  target.state = StripeState::Degraded;
  return {stripe, slot, paddr, target.valid_slots};
}

void StripeMappingTable::invalidate(std::uint64_t lpn) {
  const auto active = active_stripe(lpn);
  if (!active) throw std::runtime_error("INVALIDATE_UNMAPPED_LPN");
  auto& target = mutable_descriptor(*active);
  if (target.state != StripeState::Open &&
      target.state != StripeState::Sealed)
    throw std::runtime_error(
        "INVALIDATE_REQUIRES_OPEN_OR_SEALED_STRIPE");
  const auto slot = static_cast<std::uint32_t>(lpn -
                                                target.logical_base_lpn);
  if (!target.valid_bitmap.test(slot))
    throw std::runtime_error("INVALIDATE_NON_VALID_SLOT");
  target.valid_bitmap.clear(slot);
  target.invalid_bitmap.set(slot, stripe_capacity_);
  --target.valid_slots;
}

std::optional<StripeId> StripeMappingTable::active_stripe(
    std::uint64_t lpn) const {
  const auto it = active_.find(logical_base(lpn));
  if (it == active_.end()) return std::nullopt;
  return it->second;
}

std::optional<PhysicalAddr> StripeMappingTable::lookup(
    std::uint64_t lpn) const {
  const auto active = active_stripe(lpn);
  if (!active) return std::nullopt;
  const auto& target = descriptor(*active);
  const auto slot = static_cast<std::uint32_t>(lpn -
                                                target.logical_base_lpn);
  if (!target.valid_bitmap.test(slot)) return std::nullopt;
  return address_for(*active, slot);
}

std::optional<std::uint64_t> StripeMappingTable::reverse_lookup(
    const PhysicalAddr& paddr, std::uint32_t expected_generation) const {
  const StripeId stripe{paddr.block, expected_generation};
  const auto& target = descriptor(stripe);
  if (target.state == StripeState::Free || target.state == StripeState::Bad)
    return std::nullopt;
  const auto slot = slot_of(paddr);
  if (!target.valid_bitmap.test(slot)) return std::nullopt;
  if (const auto exception = target.exceptions.find(slot);
      exception != target.exceptions.end())
    return exception->second;
  for (const auto& extent : target.extent_runs) {
    if (slot >= extent.physical_start_slot &&
        slot < extent.physical_start_slot + extent.slot_count)
      return extent.logical_base_lpn + slot - extent.physical_start_slot;
  }
  return target.logical_base_lpn + slot;
}

void StripeMappingTable::seal(const StripeId& stripe) {
  auto& target = mutable_descriptor(stripe);
  if (target.state != StripeState::Open)
    throw std::runtime_error("SEAL_REQUIRES_OPEN_STRIPE");
  if (target.reserved_programs != 0)
    throw std::runtime_error("SEAL_WITH_PROGRAMS_IN_FLIGHT");
  target.state = StripeState::Sealed;
}

void StripeMappingTable::begin_migration(const StripeId& source) {
  auto& target = mutable_descriptor(source);
  const auto active = active_.find(target.logical_base_lpn);
  if (active == active_.end() || active->second != source)
    throw std::runtime_error("MIGRATION_SOURCE_NOT_ACTIVE");
  if (target.state != StripeState::Sealed &&
      target.state != StripeState::Degraded)
    throw std::runtime_error("MIGRATION_REQUIRES_SEALED_OR_DEGRADED_STRIPE");
  if (target.reserved_programs != 0)
    throw std::runtime_error("MIGRATION_WITH_PROGRAMS_IN_FLIGHT");
  target.state = StripeState::Migrating;
}

void StripeMappingTable::remap_commit(const StripeId& source,
                                      const StripeId& destination) {
  auto& old = mutable_descriptor(source);
  auto& replacement = mutable_descriptor(destination);
  const auto active = active_.find(old.logical_base_lpn);
  if (active == active_.end() || active->second != source)
    throw std::runtime_error("REMAP_SOURCE_NOT_ACTIVE");
  if (old.state != StripeState::Migrating)
    throw std::runtime_error("REMAP_SOURCE_NOT_MIGRATING");
  if (replacement.state != StripeState::Sealed)
    throw std::runtime_error("REMAP_DESTINATION_NOT_SEALED");
  if (replacement.logical_base_lpn != old.logical_base_lpn)
    throw std::runtime_error("REMAP_LOGICAL_RANGE_MISMATCH");
  if (replacement.failed_bitmap.any() || replacement.reserved_programs != 0)
    throw std::runtime_error("REMAP_DESTINATION_INCOMPLETE");
  for (std::uint32_t slot = 0; slot < old.next_program_slot; ++slot) {
    const bool needs_data = old.valid_bitmap.test(slot) ||
                            old.failed_bitmap.test(slot);
    if (needs_data && !replacement.valid_bitmap.test(slot))
      throw std::runtime_error("REMAP_DESTINATION_MISSING_LIVE_SLOT");
    if (!needs_data && !replacement.hole_bitmap.test(slot))
      throw std::runtime_error("REMAP_DESTINATION_MISSING_HOLE");
  }
  active->second = destination;
  old.state = StripeState::Stale;
}

void StripeMappingTable::abort_migration(const StripeId& destination) {
  auto& aborted = mutable_descriptor(destination);
  if (aborted.reserved_programs != 0)
    throw std::runtime_error("ABORT_WITH_PROGRAMS_IN_FLIGHT");
  const auto active = active_.find(aborted.logical_base_lpn);
  if (active != active_.end()) {
    auto& source = mutable_descriptor(active->second);
    if (source.state == StripeState::Migrating)
      source.state = source.failed_bitmap.any() ? StripeState::Degraded
                                                : StripeState::Sealed;
  }
  if (active != active_.end() && active->second == destination)
    throw std::runtime_error("CANNOT_ABORT_ACTIVE_STRIPE");
  aborted.state = StripeState::Stale;
}

void StripeMappingTable::on_erase(const PhysicalAddr& block_addr) {
  if (block_addr.block >= descriptors_.size())
    throw std::out_of_range("erased block outside stripe geometry");
  auto& target = descriptors_.at(block_addr.block);
  if (target.state == StripeState::Free) return;
  if (block_addr.generation != 0 &&
      block_addr.generation != target.id.generation)
    throw std::runtime_error("STALE_GENERATION_ON_ERASE");
  const auto lane =
      (block_addr.stack * config_.dies_per_stack + block_addr.die) *
          config_.planes_per_die +
      block_addr.plane;
  if (!target.erased_lane_bitmap.test(lane)) {
    target.erased_lane_bitmap.set(lane, stripe_width_);
    ++target.erased_lanes;
  }
  target.state = StripeState::Erasing;
  if (target.erased_lanes != stripe_width_) return;

  if (const auto active = active_.find(target.logical_base_lpn);
      active != active_.end() && active->second == target.id)
    active_.erase(active);
  const auto physical = target.id.physical_id;
  StripeDescriptor fresh;
  fresh.id = {physical, target.id.generation};
  descriptors_.at(physical) = std::move(fresh);
  free_stripes_.push_back(physical);
}

bool StripeMappingTable::validate_generation(
    const PhysicalAddr& paddr) const {
  if (paddr.physical_stripe ==
      std::numeric_limits<std::uint64_t>::max())
    return true;
  if (paddr.physical_stripe >= descriptors_.size()) return false;
  const auto& descriptor = descriptors_.at(paddr.physical_stripe);
  return descriptor.state != StripeState::Free &&
         descriptor.id.generation == paddr.generation &&
         paddr.generation != 0;
}

}  // namespace hbfsim
