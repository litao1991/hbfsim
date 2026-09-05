#include "hbfsim/core.h"

#include "test_support.h"
#include <set>
#include <tuple>

namespace {

auto key(const hbfsim::PhysicalAddr& p) {
  return std::tuple{p.stack, p.die, p.plane, p.block, p.page};
}

}  // namespace

int main() {
  hbfsim::Config c;
  c.stacks = 1; c.dies_per_stack = 16; c.planes_per_die = 32;
  c.mapping_policy = hbfsim::MappingPolicy::FineStripe;
  hbfsim::AddressMapper mapper(c);
  const auto first = mapper.map_read(0);
  const auto last = mapper.map_read(511);
  const auto next = mapper.map_read(512);
  CHECK(first.die == 0 && first.plane == 0 && first.page == 0);
  CHECK(last.die == 15 && last.plane == 31 && last.page == 0);
  CHECK(next.die == 0 && next.plane == 0 && next.page == 1);

  c.stacks = 2;
  c.dies_per_stack = 1;
  c.planes_per_die = 4;
  c.blocks_per_plane = 2;
  c.pages_per_block = 8;
  c.max_active_planes_per_die = 4;
  c.max_active_planes_per_stack = 4;
  c.page_size = 4096;
  c.burst_size = 8192;
  c.mapping_policy = hbfsim::MappingPolicy::BurstStripe;
  hbfsim::AddressMapper burst_mapper(c);
  std::set<decltype(key(burst_mapper.map_read(0)))> addresses;
  for (std::uint64_t lpn = 0; lpn < 32; ++lpn)
    CHECK(addresses.insert(key(burst_mapper.map_read(lpn))).second);
  CHECK(burst_mapper.map_read(0).stack == 0);
  CHECK(burst_mapper.map_read(2).stack == 1);
  CHECK(burst_mapper.map_read(4).plane == 2);
}
