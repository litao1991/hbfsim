#include "hbfsim/core.h"

#include <algorithm>
#include <stdexcept>

namespace hbfsim {

AddressMapper::AddressMapper(const Config& config) : config_(config) {
  config_.validate();
  const auto total_planes = static_cast<std::uint64_t>(config_.stacks) *
                            config_.dies_per_stack * config_.planes_per_die;
  frontiers_.assign(static_cast<std::size_t>(total_planes), 0);
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
  if (const auto it = l2p_.find(lpn); it != l2p_.end()) return it->second;
  return base_map(lpn);
}

PhysicalAddr AddressMapper::placement(std::uint64_t lpn) const {
  return base_map(lpn);
}

PhysicalAddr AddressMapper::preview_write(std::uint64_t lpn) const {
  PhysicalAddr target = base_map(lpn);
  if (config_.mapping_policy != MappingPolicy::HostManaged) return target;
  const auto page_number = frontiers_.at(flat_plane(target));
  const std::uint64_t capacity =
      static_cast<std::uint64_t>(config_.blocks_per_plane) *
      config_.pages_per_block;
  if (page_number >= capacity)
    throw std::runtime_error("host-managed write frontier exhausted a plane");
  target.block = static_cast<std::uint32_t>(page_number / config_.pages_per_block);
  target.page = static_cast<std::uint32_t>(page_number % config_.pages_per_block);
  return target;
}

PhysicalAddr AddressMapper::allocate_host_managed(std::uint64_t lpn) {
  PhysicalAddr target = base_map(lpn);
  const auto index = flat_plane(target);
  const auto page_number = frontiers_.at(index)++;
  const std::uint64_t capacity = static_cast<std::uint64_t>(config_.blocks_per_plane) * config_.pages_per_block;
  if (page_number >= capacity) throw std::runtime_error("host-managed write frontier exhausted a plane");
  target.block = static_cast<std::uint32_t>(page_number / config_.pages_per_block);
  target.page = static_cast<std::uint32_t>(page_number % config_.pages_per_block);
  return target;
}

PhysicalAddr AddressMapper::prepare_write(std::uint64_t lpn) {
  if (config_.mapping_policy == MappingPolicy::HostManaged) return allocate_host_managed(lpn);
  return base_map(lpn);
}

void AddressMapper::commit_write(std::uint64_t lpn, const PhysicalAddr& paddr) {
  if (config_.mapping_policy == MappingPolicy::HostManaged) l2p_[lpn] = paddr;
}

std::optional<PhysicalAddr> AddressMapper::lookup(std::uint64_t lpn) const {
  if (const auto it = l2p_.find(lpn); it != l2p_.end()) return it->second;
  return std::nullopt;
}

void AddressMapper::on_erase(const PhysicalAddr& block_addr) {
  const auto index = flat_plane(block_addr);
  const auto block_begin = static_cast<std::uint64_t>(block_addr.block) * config_.pages_per_block;
  const auto block_end = block_begin + config_.pages_per_block;
  if (config_.mapping_policy == MappingPolicy::HostManaged && frontiers_.at(index) >= block_begin && frontiers_.at(index) <= block_end)
    frontiers_.at(index) = block_begin;
  for (auto it = l2p_.begin(); it != l2p_.end();) {
    const auto& p = it->second;
    if (p.stack == block_addr.stack && p.die == block_addr.die && p.plane == block_addr.plane && p.block == block_addr.block)
      it = l2p_.erase(it);
    else ++it;
  }
}

}  // namespace hbfsim
