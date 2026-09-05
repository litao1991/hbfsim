#include "hbfsim/core.h"

#include "../test_support.h"

int main() {
  using namespace hbfsim;
  Config config;
  config.stacks = 1;
  config.host_channels_per_stack = 2;
  config.axi_ports_per_channel = 2;
  config.axi_id_count = 8;
  config.axi_max_outstanding_per_id = 2;
  AxiOrderTracker axi(config);

  const AxiEndpoint same_id{0, 0, 3};
  const AxiEndpoint other_id{0, 0, 4};
  CHECK(axi.issue(same_id, 10) == HbfStatus::Success);
  CHECK(axi.issue(same_id, 11) == HbfStatus::Success);
  CHECK(axi.issue(same_id, 12) == HbfStatus::TemporarilyRestricted);
  CHECK(axi.issue(other_id, 20) == HbfStatus::Success);
  CHECK(axi.issue({2, 0, 0}, 30) == HbfStatus::InvalidUserField);
  CHECK(axi.issue({0, 2, 0}, 31) == HbfStatus::InvalidUserField);
  CHECK(axi.issue({0, 0, 8}, 32) == HbfStatus::InvalidUserField);

  CHECK(axi.complete(HbfResponse::success(11)).empty());
  auto independent = axi.complete(HbfResponse::success(20));
  CHECK(independent.size() == 1);
  CHECK(independent.front().request_id == 20);
  auto ordered = axi.complete(HbfResponse::success(10));
  CHECK(ordered.size() == 2);
  CHECK(ordered[0].request_id == 10);
  CHECK(ordered[1].request_id == 11);
  CHECK(axi.total_outstanding() == 0);
  return 0;
}
