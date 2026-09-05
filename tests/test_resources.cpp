#include "hbfsim/core.h"

#include "test_support.h"

int main() {
  hbfsim::LinkResource link(10.0, 100);
  CHECK(link.reserve(0, 100) == 110);
  CHECK(link.reserve(0, 100) == 120);
  CHECK(link.free_at() == 20);

  hbfsim::DataFabric fabric(2, 20.0, 10.0, 100);
  CHECK(fabric.reserve(0, 100, 0) == 110);
  CHECK(fabric.reserve(0, 100, 1) == 115);
  CHECK(fabric.reserve(0, 100, 0) == 120);

  hbfsim::Config config;
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 2;
  config.ports_per_stack = 1;
  config.host_channels_per_stack = 2;
  config.max_active_planes_per_die = 2;
  config.max_active_planes_per_stack = 2;
  config.mapping_policy = hbfsim::MappingPolicy::FineStripe;
  hbfsim::AddressMapper mapper(config);
  hbfsim::HostRouter router(config);
  const auto media0 = mapper.placement(0);
  const auto media1 = mapper.placement(1);
  CHECK(media0.data_port == media1.data_port);
  CHECK(router.route(0, media0).channel !=
        router.route(config.page_size, media1).channel);

  hbfsim::HostInterface duplex(1, 1.0, 0, true);
  CHECK(duplex.reserve({0, 0}, hbfsim::HostLinkDirection::HostToDevice,
                       0, 100).completion == 100);
  CHECK(duplex.reserve({0, 0}, hbfsim::HostLinkDirection::DeviceToHost,
                       0, 100).completion == 100);
  hbfsim::HostInterface half_duplex(1, 1.0, 0, false);
  CHECK(half_duplex.reserve({0, 0},
                            hbfsim::HostLinkDirection::HostToDevice,
                            0, 100).completion == 100);
  CHECK(half_duplex.reserve({0, 0},
                            hbfsim::HostLinkDirection::DeviceToHost,
                            0, 100).completion == 200);
}
