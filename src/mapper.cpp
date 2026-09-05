#include "hbfsim/core.h"

#include <algorithm>
#include <stdexcept>

namespace hbfsim {

AddressMapper::AddressMapper(const Config& config) : config_(config) {
  config_.validate();
  if (config_.mapping_policy == MappingPolicy::HostManaged)
    stripes_ = std::make_unique<StripeMappingTable>(config_);
}

std::uint32_t AddressMapper::flat_plane(const PhysicalAddr& a) const {
  return (static_cast<std::uint32_t>(a.stack) * config_.dies_per_stack + a.die) * config_.planes_per_die + a.plane;
}

PhysicalAddr AddressMapper::base_map(std::uint64_t lpn) const {
  const std::uint64_t planes_per_stack =
      static_cast<std::uint64_t>(config_.dies_per_stack) *
      config_.planes_per_die;
  const std::uint64_t total_planes =
      static_cast<std::uint64_t>(config_.stacks) * planes_per_stack;
  const std::uint64_t capacity_pages = total_planes * config_.blocks_per_plane * config_.pages_per_block;
  if (lpn >= capacity_pages) throw std::out_of_range("logical page exceeds configured HBF capacity");
  PhysicalAddr p;
  std::uint64_t stripe = 0, row = 0;
  if (config_.mapping_policy == MappingPolicy::Linear) {
    p.page = static_cast<std::uint32_t>(lpn % config_.pages_per_block);
    auto x = lpn / config_.pages_per_block;
    p.block = static_cast<std::uint32_t>(x % config_.blocks_per_plane); x /= config_.blocks_per_plane;
    p.plane = static_cast<std::uint32_t>(x % config_.planes_per_die); x /= config_.planes_per_die;
    p.die = static_cast<std::uint32_t>(x % config_.dies_per_stack); x /= config_.dies_per_stack;
    p.stack = static_cast<std::uint32_t>(x % config_.stacks);
  } else if (config_.mapping_policy == MappingPolicy::BurstStripe) {
    const std::uint64_t slices = std::max<std::uint64_t>(1, config_.burst_size / config_.page_size);
    const std::uint64_t burst = lpn / slices;
    const std::uint64_t slice = lpn % slices;
    p.stack = static_cast<std::uint32_t>(burst % config_.stacks);
    const std::uint64_t page_within_stack =
        (burst / config_.stacks) * slices + slice;
    row = page_within_stack / planes_per_stack;
    stripe = page_within_stack % planes_per_stack;
    p.plane = static_cast<std::uint32_t>(stripe % config_.planes_per_die);
    p.die = static_cast<std::uint32_t>(stripe / config_.planes_per_die);
    p.page = static_cast<std::uint32_t>(row % config_.pages_per_block);
    p.block = static_cast<std::uint32_t>(row / config_.pages_per_block);
  } else {
    stripe = lpn % total_planes; row = lpn / total_planes;
    p.plane = static_cast<std::uint32_t>(stripe % config_.planes_per_die); stripe /= config_.planes_per_die;
    p.die = static_cast<std::uint32_t>(stripe % config_.dies_per_stack); stripe /= config_.dies_per_stack;
    p.stack = static_cast<std::uint32_t>(stripe % config_.stacks);
    p.page = static_cast<std::uint32_t>(row % config_.pages_per_block);
    p.block = static_cast<std::uint32_t>((row / config_.pages_per_block) % config_.blocks_per_plane);
  }
  p.data_port = flat_plane(p) % config_.ports_per_stack;
  return p;
}

PhysicalAddr AddressMapper::map_read(std::uint64_t lpn) const {
  if (stripes_) {
    if (const auto mapped = stripes_->lookup(lpn)) return *mapped;
    if (const auto active = stripes_->active_stripe(lpn)) {
      const auto& descriptor = stripes_->descriptor(*active);
      const auto slot = static_cast<std::uint32_t>(
          lpn - descriptor.logical_base_lpn);
      return stripes_->address_for(*active, slot);
    }
  }
  return base_map(lpn);
}

PhysicalAddr AddressMapper::placement(std::uint64_t lpn) const {
  if (stripes_) return stripes_->preview_program(lpn);
  return base_map(lpn);
}

PhysicalAddr AddressMapper::preview_write(std::uint64_t lpn) const {
  return stripes_ ? stripes_->preview_program(lpn) : base_map(lpn);
}

PhysicalAddr AddressMapper::prepare_write(std::uint64_t lpn) {
  if (stripes_) return stripes_->reserve_program(lpn);
  return base_map(lpn);
}

void AddressMapper::commit_write(std::uint64_t lpn, const PhysicalAddr& paddr,
                                 SimTime now) {
  if (stripes_) stripes_->commit_program(lpn, paddr, now);
}

ProgramFailureNotice AddressMapper::fail_write(
    std::uint64_t lpn, const PhysicalAddr& paddr) {
  if (!stripes_)
    return {{}, 0, paddr, 0};
  return stripes_->fail_program(lpn, paddr);
}

std::optional<PhysicalAddr> AddressMapper::lookup(std::uint64_t lpn) const {
  if (stripes_) return stripes_->lookup(lpn);
  return std::nullopt;
}

void AddressMapper::on_erase(const PhysicalAddr& block_addr) {
  if (stripes_) stripes_->on_erase(block_addr);
}

bool AddressMapper::validate_generation(const PhysicalAddr& paddr) const {
  return !stripes_ || stripes_->validate_generation(paddr);
}

}  // namespace hbfsim
