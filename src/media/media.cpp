#include "hbfsim/media/nand_media.h"

#include <algorithm>

namespace hbfsim {
namespace {

bool same_page(const PhysicalAddr& left, const PhysicalAddr& right,
               bool include_generation) {
  return left.stack == right.stack && left.die == right.die &&
         left.plane == right.plane && left.block == right.block &&
         left.page == right.page &&
         (!include_generation || left.generation == right.generation);
}

bool same_block(const PhysicalAddr& left, const PhysicalAddr& right) {
  return left.stack == right.stack && left.die == right.die &&
         left.plane == right.plane && left.block == right.block;
}

bool bitmap_test(const std::vector<std::uint64_t>& bitmap,
                 std::uint32_t page) {
  const auto word = page / 64;
  return word < bitmap.size() &&
         (bitmap[word] & (1ULL << (page % 64))) != 0;
}

void bitmap_set(std::vector<std::uint64_t>& bitmap, std::uint32_t pages,
                std::uint32_t page) {
  if (bitmap.empty()) bitmap.resize((pages + 63) / 64, 0);
  bitmap.at(page / 64) |= 1ULL << (page % 64);
}

}  // namespace

std::uint32_t NandTopology::flat_plane(
    const PhysicalAddr& address) const {
  return (address.stack * config_.dies_per_stack + address.die) *
             config_.planes_per_die +
         address.plane;
}

std::uint32_t NandTopology::flat_die(const PhysicalAddr& address) const {
  return address.stack * config_.dies_per_stack + address.die;
}

std::uint32_t NandTopology::bank_of_plane(std::uint32_t plane) const {
  return plane % config_.banks_per_die;
}

std::uint32_t NandTopology::flat_bank(const PhysicalAddr& address) const {
  return flat_die(address) * config_.banks_per_die + address.bank;
}

std::size_t NandTopology::plane_count() const {
  return static_cast<std::size_t>(config_.stacks) *
         config_.dies_per_stack * config_.planes_per_die;
}

std::size_t NandTopology::die_count() const {
  return static_cast<std::size_t>(config_.stacks) * config_.dies_per_stack;
}

std::size_t NandTopology::bank_count() const {
  return die_count() * config_.banks_per_die;
}

bool BankReadCache::lookup(const PhysicalAddr& page, SimTime now) {
  for (auto& entry : entries_) {
    if (entry.valid && entry.ready_at <= now &&
        same_page(entry.page, page, true)) {
      entry.last_use = ++lru_clock_;
      return true;
    }
  }
  return false;
}

bool BankReadCache::fill(const PhysicalAddr& page, SimTime ready_at) {
  auto victim = entries_.end();
  for (auto it = entries_.begin(); it != entries_.end(); ++it) {
    if (it->valid && same_page(it->page, page, true)) {
      victim = it;
      break;
    }
    if (!it->valid) {
      victim = it;
      break;
    }
    if (victim == entries_.end() || it->last_use < victim->last_use)
      victim = it;
  }
  if (victim == entries_.end()) return false;
  const bool evicted = victim->valid;
  *victim = {true, page, ready_at, ++lru_clock_};
  return evicted;
}

void BankReadCache::invalidate_page(const PhysicalAddr& page) {
  for (auto& entry : entries_)
    if (entry.valid && same_page(entry.page, page, false))
      entry.valid = false;
}

void BankReadCache::invalidate_block(const PhysicalAddr& block) {
  for (auto& entry : entries_)
    if (entry.valid && same_block(entry.page, block)) entry.valid = false;
}

NandMediaSystem::NandMediaSystem(const Config& config,
                                 const NandTopology& topology)
    : config_(config), topology_(topology), planes_(topology.plane_count()),
      dies_(topology.die_count()), banks_(topology.bank_count()) {
  for (auto& target : planes_)
    target.blocks.resize(config_.blocks_per_plane);
  for (auto& bank : banks_)
    bank.read_cache = BankReadCache(config_.read_cache_entries_per_bank);
}

Plane& NandMediaSystem::plane(const PhysicalAddr& address) {
  return planes_.at(topology_.flat_plane(address));
}

const Plane& NandMediaSystem::plane(const PhysicalAddr& address) const {
  return planes_.at(topology_.flat_plane(address));
}

DieState& NandMediaSystem::die(const PhysicalAddr& address) {
  return dies_.at(topology_.flat_die(address));
}

const DieState& NandMediaSystem::die(const PhysicalAddr& address) const {
  return dies_.at(topology_.flat_die(address));
}

BankState& NandMediaSystem::bank(const PhysicalAddr& address) {
  return banks_.at(topology_.flat_bank(address));
}

const BankState& NandMediaSystem::bank(const PhysicalAddr& address) const {
  return banks_.at(topology_.flat_bank(address));
}

std::uint64_t NandMediaSystem::page_key(const PhysicalAddr& address) const {
  return block_key(address) * config_.pages_per_block + address.page;
}

std::uint64_t NandMediaSystem::block_key(const PhysicalAddr& address) const {
  return static_cast<std::uint64_t>(topology_.flat_plane(address)) *
             config_.blocks_per_plane +
         address.block;
}

PageState NandMediaSystem::page_state(const PhysicalAddr& address) const {
  if (const auto it = transient_page_states_.find(page_key(address));
      it != transient_page_states_.end())
    return it->second;
  const auto& block = plane(address).blocks.at(address.block);
  if (bitmap_test(block.failed_bitmap, address.page))
    return PageState::Failed;
  if (bitmap_test(block.valid_bitmap, address.page))
    return PageState::Valid;
  if (bitmap_test(block.invalid_bitmap, address.page))
    return PageState::Invalid;
  return PageState::Erased;
}

BlockState NandMediaSystem::block_state(const PhysicalAddr& address) const {
  return plane(address).blocks.at(address.block).state;
}

SimTime NandMediaSystem::block_ready_at(const PhysicalAddr& address) const {
  return plane(address).blocks.at(address.block).ready_at;
}

std::uint32_t NandMediaSystem::block_erase_count(
    const PhysicalAddr& address) const {
  return plane(address).blocks.at(address.block).erase_count;
}

SimTime NandMediaSystem::die_ready_at(const PhysicalAddr& address) const {
  const auto& state = die(address);
  return std::max(state.ready_at, state.command_ready_at);
}

bool NandMediaSystem::read_cache_lookup(const PhysicalAddr& address,
                                        SimTime now) {
  return config_.read_cache_enabled && bank(address).read_cache.lookup(address, now);
}

bool NandMediaSystem::read_cache_fill(const PhysicalAddr& address,
                                      SimTime ready_at) {
  return config_.read_cache_enabled &&
         bank(address).read_cache.fill(address, ready_at);
}

void NandMediaSystem::invalidate_read_cache_page(
    const PhysicalAddr& address) {
  if (config_.read_cache_enabled)
    bank(address).read_cache.invalidate_page(address);
}

void NandMediaSystem::invalidate_read_cache_block(
    const PhysicalAddr& address) {
  if (config_.read_cache_enabled)
    bank(address).read_cache.invalidate_block(address);
}

void NandMediaSystem::mark_erased(const PhysicalAddr& address) {
  erased_blocks_.insert(block_key(address));
}

void NandMediaSystem::set_transient_page_state(
    const PhysicalAddr& address, PageState state) {
  transient_page_states_[page_key(address)] = state;
}

void NandMediaSystem::clear_transient_page_state(
    const PhysicalAddr& address) {
  transient_page_states_.erase(page_key(address));
}

void NandMediaSystem::materialize_initialized_page(
    const PhysicalAddr& address) {
  auto& block = plane(address).blocks.at(address.block);
  if (erased_blocks_.contains(block_key(address)) || block.bad ||
      bitmap_test(block.valid_bitmap, address.page) ||
      page_state(address) != PageState::Erased)
    return;
  bitmap_set(block.valid_bitmap, config_.pages_per_block, address.page);
  ++block.valid_pages;
  block.next_program_page =
      std::max(block.next_program_page, address.page + 1);
  block.state = block.next_program_page == config_.pages_per_block
                    ? BlockState::Closed
                    : BlockState::Open;
}

bool NandMediaSystem::retire_block(const PhysicalAddr& address) {
  invalidate_read_cache_block(address);
  auto& block = plane(address).blocks.at(address.block);
  if (block.bad) return false;
  block.bad = true;
  block.state = BlockState::Bad;
  return true;
}

}  // namespace hbfsim
