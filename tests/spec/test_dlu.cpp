#include "hbfsim/core.h"

#include "../test_support.h"

namespace {

hbfsim::Config config() {
  hbfsim::Config value;
  value.simulation_profile = hbfsim::SimulationProfile::HbfV07;
  value.research_stripe_mapping_enabled = false;
  value.research_copy_gc_enabled = false;
  value.research_migration_recovery_enabled = false;
  value.stacks = 1;
  value.dies_per_stack = 1;
  value.planes_per_die = 1;
  value.blocks_per_plane = 1;
  value.pages_per_block = 2;
  value.page_size = 4096;
  value.host_channels_per_stack = 1;
  value.ports_per_stack = 1;
  value.max_active_planes_per_die = 1;
  value.max_active_planes_per_stack = 1;
  value.mapping_policy = hbfsim::MappingPolicy::Linear;
  value.dlu_accumulation_timeout_ns = 100;
  value.host_bw_bytes_per_ns = 1000;
  value.internal_bw_bytes_per_ns = 1000;
  value.internal_port_bw_bytes_per_ns = 1000;
  value.host_fixed_latency_ns = 0;
  value.internal_fixed_latency_ns = 0;
  value.program_ns = 10;
  value.erase_ns = 20;
  return value;
}

}  // namespace

int main() {
  using namespace hbfsim;
  {
    auto value = config();
    DluAssembler assembler(value);
    auto first = assembler.submit(1, {0, 0}, 2048, 0);
    CHECK(first.status == HbfStatus::Pending);
    CHECK(first.deadline == 100);
    CHECK(assembler.submit(2, {0, 1024}, 1024, 1).status ==
          HbfStatus::OverlappingAddress);
    auto second = assembler.submit(3, {0, 2048}, 2048, 2);
    CHECK(second.status == HbfStatus::Success);
    CHECK(second.completed->size == 4096);
    CHECK(second.completed->request_ids.size() == 2);
    CHECK(assembler.pending_count() == 0);

    CHECK(assembler.submit(4, {0, 4096}, 1024, 10).status ==
          HbfStatus::Pending);
    auto expired = assembler.expire(110);
    CHECK(expired.size() == 1);
    CHECK(expired[0].request_ids[0] == 4);
    CHECK(expired[0].status == HbfStatus::DluAccumulationTimeout);
  }

  {
    auto value = config();
    value.max_pending_dlus = 1;
    DluAssembler assembler(value);
    CHECK(assembler.submit(1, {0, 0}, 64, 0).status == HbfStatus::Pending);
    CHECK(assembler.submit(2, {0, 4096}, 64, 0).status ==
          HbfStatus::MaxPendingDluReached);
    CHECK(assembler.submit(3, {1, 0}, 64, 0).status == HbfStatus::Pending);
  }

  {
    auto value = config();
    DluAssembler assembler(value);
    CHECK(assembler.submit(1, {0, 0}, 64, 0).status == HbfStatus::Pending);
    const auto forwarded = assembler.lookup({0, 0}, 64);
    CHECK(forwarded.disposition == DluReadDisposition::Forwarded);
    CHECK(forwarded.ready_at == 0);
    const auto missing = assembler.lookup({0, 64}, 64);
    CHECK(missing.disposition == DluReadDisposition::PendingWrite);
    CHECK(missing.status == HbfStatus::ReadPendingWrite);
  }

  {
    auto value = config();
    DluAssembler assembler(value);
    const auto first = assembler.submit(1, {0, 0}, 64, 0, 25);
    CHECK(first.deadline == 125);
    const auto second = assembler.submit(2, {0, 64}, 64, 1, 40);
    CHECK(!second.deadline.has_value());
    const auto forwarded = assembler.lookup({0, 0}, 128);
    CHECK(forwarded.disposition == DluReadDisposition::Forwarded);
    CHECK(forwarded.ready_at == 40);
  }

  {
    auto value = config();
    Simulator simulator(value);
    simulator.submit({0, OpType::Write, 0, 4096, 0});
    simulator.submit({0, OpType::Write, 4096, 4096, 0});
    simulator.run();
    CHECK(simulator.block_erase_count({0, 0, 0, 0, 0}) == 0);
    const auto before = simulator.now();
    simulator.submit({before, OpType::Write, 0, 4096, 0});
    simulator.run();
    CHECK(simulator.block_erase_count({0, 0, 0, 0, 0}) == 1);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) == PageState::Valid);
    CHECK(simulator.page_state({0, 0, 0, 0, 1}) == PageState::Erased);
    CHECK(simulator.now() >= before + value.erase_ns + value.program_ns);
    CHECK(simulator.responses().back().status == HbfStatus::Success);
  }


  {
    auto value = config();
    Simulator simulator(value);
    for (std::uint64_t fragment = 0; fragment < 64; ++fragment)
      simulator.submit({0, OpType::Write, fragment * 64, 64, 0,
                        static_cast<std::uint32_t>(fragment % 4)});
    simulator.run();
    CHECK(simulator.stats().completed_requests() == 64);
    CHECK(simulator.responses().size() == 64);
    CHECK(simulator.page_state({0, 0, 0, 0, 0}) == PageState::Valid);
    CHECK(simulator.system().dlu_assembler().pending_count() == 0);
  }

  {
    auto value = config();
    Simulator simulator(value);
    simulator.submit({0, OpType::Write, 0, 64, 0, 1});
    simulator.submit({3, OpType::Read, 0, 64, 0, 2});
    simulator.run_until(10);
    CHECK(simulator.responses().size() == 1);
    CHECK(simulator.responses()[0].request_id == 1);
    CHECK(simulator.responses()[0].status == HbfStatus::Success);

    simulator.submit({10, OpType::Read, 64, 64, 0, 3});
    simulator.run_until(20);
    CHECK(simulator.responses().size() == 2);
    CHECK(simulator.responses()[1].status == HbfStatus::ReadPendingWrite);
    CHECK(hbf_status_code(OpType::Read,
                          simulator.responses()[1].status) == 0xA);
    simulator.run();
    CHECK(simulator.responses().size() == 3);
    CHECK(simulator.responses()[2].status ==
          HbfStatus::DluAccumulationTimeout);
    CHECK(hbf_status_code(OpType::Write,
                          simulator.responses()[2].status) == 0x5);
  }
  return 0;
}
