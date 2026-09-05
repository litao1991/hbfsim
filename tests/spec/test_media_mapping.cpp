#include "hbfsim/core.h"

#include "../test_support.h"

namespace {

hbfsim::Config config(hbfsim::ChannelMediaPolicy policy) {
  auto value = hbfsim::Config::for_profile(hbfsim::SimulationProfile::HbfV07);
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 4;
  value.banks_per_die = 2;
  value.blocks_per_plane = 2;
  value.pages_per_block = 8;
  value.host_channels_per_stack = 1;
  value.hbf_channel_count = 1;
  value.ports_per_stack = 4;
  value.max_active_planes_per_die = 4;
  value.max_active_planes_per_stack = 4;
  value.channel_media_policy = policy;
  return value;
}

}  // namespace

int main() {
  using namespace hbfsim;
  const auto striped_config = config(ChannelMediaPolicy::FineStripe);
  AddressMapper striped(striped_config);
  for (std::uint64_t lpn = 0; lpn < 4; ++lpn) {
    const auto address = striped.map_channel_read({0, lpn * 4096});
    CHECK(address.plane == lpn);
    CHECK(address.page == 0);
    CHECK(address.bank == lpn % 2);
  }
  CHECK(striped.map_channel_read({0, 4 * 4096}).page == 1);

  const auto linear_config = config(ChannelMediaPolicy::Linear);
  AddressMapper linear(linear_config);
  CHECK(linear.map_channel_read({0, 0}).plane == 0);
  CHECK(linear.map_channel_read({0, 4096}).plane == 0);
  CHECK(linear.map_channel_read({0, 4096}).page == 1);
  return 0;
}
