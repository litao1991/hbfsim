#include "hbfsim/core.h"

#include "test_support.h"

int main() {
  {
    hbfsim::ResourceTracker tracker;
    tracker.configure(1, 1, 2, 1, 1);
    tracker.transition(hbfsim::ResourceKind::Array, 0, 0, 1, 0);
    tracker.transition(hbfsim::ResourceKind::Host, 0, 0, 1, 3);
    tracker.transition(hbfsim::ResourceKind::Fabric, 0, 0, 1, 5);
    tracker.transition(hbfsim::ResourceKind::Host, 0, 0, -1, 8);
    tracker.transition(hbfsim::ResourceKind::Array, 0, 1, 1, 10);
    tracker.transition(hbfsim::ResourceKind::Array, 0, 0, -1, 15);
    tracker.transition(hbfsim::ResourceKind::Fabric, 0, 0, -1, 20);
    tracker.transition(hbfsim::ResourceKind::Array, 0, 1, -1, 25);
    CHECK(tracker.array_busy(0, 25) == 25);
    CHECK(tracker.fabric_busy(0, 25) == 15);
    CHECK(tracker.host_busy(0, 25) == 5);
    CHECK(tracker.array_fabric_overlap(0, 25) == 15);
    CHECK(tracker.active_plane_area(0, 25) == 30);
    CHECK(tracker.max_active_planes(0) == 2);
    CHECK(tracker.plane_busy(0, 25) == 15);
    CHECK(tracker.plane_busy(1, 25) == 15);
    CHECK(tracker.die_busy(0, 25) == 25);
    CHECK(tracker.port_busy(0, 25) == 15);
    CHECK(tracker.host_channel_busy(0, 25) == 5);
  }

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

  config.read_ns = 10;
  config.host_fixed_latency_ns = 0;
  config.internal_fixed_latency_ns = 0;
  config.queue_depth_sample_interval_ns = 1'000'000;
  hbfsim::Simulator simulator(config);
  for (std::uint64_t page = 0; page < 16; ++page)
    simulator.submit({0, hbfsim::OpType::Read,
                      page * config.page_size, config.page_size, 0});
  simulator.run();
  CHECK(simulator.stats().queue_depth_sample_count() <= 2);
}
