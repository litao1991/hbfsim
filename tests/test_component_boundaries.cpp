#include "hbfsim/controller/media_scheduler.h"
#include "hbfsim/hbf_system.h"
#include "hbfsim/media/read_cache.h"

#include "test_support.h"

#include <unordered_map>

namespace {

hbfsim::Config small_config() {
  using namespace hbfsim;
  auto config = Config::for_profile(SimulationProfile::HbfV07);
  config.stacks = 1;
  config.dies_per_stack = 1;
  config.planes_per_die = 4;
  config.banks_per_die = 2;
  config.blocks_per_plane = 2;
  config.pages_per_block = 4;
  config.host_channels_per_stack = 1;
  config.hbf_channel_count = 1;
  config.ports_per_stack = 4;
  config.max_active_planes_per_die = 4;
  config.max_active_planes_per_stack = 4;
  config.mapping_policy = MappingPolicy::Linear;
  config.validate();
  return config;
}

}  // namespace

int main() {
  using namespace hbfsim;
  const auto config = small_config();
  HbfSystem system(config);

  CHECK(&system.topology() == &system.mapper().topology());
  CHECK(&system.topology() == &system.media().topology());
  CHECK(&system.frontend().axi() == &system.axi());
  CHECK(&system.frontend().dlu_assembler() == &system.dlu_assembler());
  CHECK(system.topology().bank_of_plane(3) == 1);
  PhysicalAddr physical{0, 0, 3, 0, 0};
  physical.bank = 1;
  CHECK(system.topology().flat_plane(physical) == 3);
  CHECK(system.topology().flat_bank(physical) == 1);

  BankReadCache cache(2);
  PhysicalAddr first{0, 0, 0, 0, 0};
  PhysicalAddr second{0, 0, 0, 0, 1};
  PhysicalAddr third{0, 0, 0, 0, 2};
  CHECK(!cache.fill(first, 10));
  CHECK(!cache.fill(second, 10));
  CHECK(cache.lookup(first, 10));
  CHECK(cache.fill(third, 10));
  CHECK(!cache.lookup(second, 10));
  CHECK(cache.lookup(first, 10));
  CHECK(cache.lookup(third, 10));

  auto accepted = system.frontend().admit(
      {0, OpType::Read, 0, 64, 0}, 7, true);
  CHECK(accepted.accepted());
  CHECK(system.axi().total_outstanding() == 1);
  accepted.request.complete_time = 100;
  const auto responses = system.frontend().complete(accepted.request);
  CHECK(responses.size() == 1);
  CHECK(responses.front().ok());
  CHECK(system.axi().total_outstanding() == 0);

  auto rejected = system.frontend().admit(
      {0, OpType::Read, 32, 64, 0}, 8, true);
  CHECK(!rejected.accepted());
  CHECK(rejected.rejection->status == HbfStatus::InvalidUserField);

  Plane plane;
  std::unordered_map<std::uint64_t, SubRequest> requests;
  SubRequest gc;
  gc.id = 1;
  gc.op = OpType::Write;
  gc.source = TransactionSource::GarbageCollection;
  requests.emplace(gc.id, gc);
  SubRequest read;
  read.id = 2;
  read.op = OpType::Read;
  read.source = TransactionSource::User;
  requests.emplace(read.id, read);
  system.controller().scheduler().enqueue(plane, requests.at(gc.id));
  system.controller().scheduler().enqueue(plane, requests.at(read.id));
  const auto selected = system.controller().scheduler().choose(
      plane, 0, [&](std::uint64_t id) -> const SubRequest& {
        return requests.at(id);
      });
  CHECK(selected == read.id);
  return 0;
}
