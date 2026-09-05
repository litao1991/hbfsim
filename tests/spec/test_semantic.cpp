#include "hbfsim/core.h"

#include "../test_support.h"

int main() {
  using namespace hbfsim;

  const auto spec = Config::for_profile(SimulationProfile::HbfV07);
  CHECK(spec.simulation_profile == SimulationProfile::HbfV07);
  CHECK(!spec.research_stripe_mapping_enabled);
  CHECK(!spec.research_copy_gc_enabled);
  CHECK(!spec.research_migration_recovery_enabled);
  CHECK(spec.channel_media_policy == ChannelMediaPolicy::FineStripe);
  CHECK(spec.read_cache_enabled);

  const auto research = Config::for_profile(SimulationProfile::MediaResearch);
  CHECK(research.research_stripe_mapping_enabled);
  CHECK(research.channel_media_policy == ChannelMediaPolicy::Linear);
  CHECK(!research.read_cache_enabled);

  CHECK(hbf_completion_class(HbfStatus::CorrectedEccRefreshRequired) ==
        HbfCompletionClass::SuccessWithAdvisory);
  CHECK(hbf_data_valid(HbfStatus::CorrectedEccRefreshRequired));
  HbfErrorInfo advisory_info;
  advisory_info.reason = "refresh recommended";
  const auto advisory = HbfResponse::failure(
      1, HbfStatus::CorrectedEccRefreshRequired,
      std::move(advisory_info), 10, 64);
  CHECK(advisory.ok());
  CHECK(advisory.data_valid());

  StatsCollector stats;
  Request request;
  request.id = 1;
  request.op = OpType::Read;
  request.size = 64;
  request.complete_time = 10;
  request.status = HbfStatus::CorrectedEccRefreshRequired;
  stats.record_request(request);
  CHECK(stats.failed_requests() == 0);
  CHECK(stats.advisory_requests() == 1);

  auto config = Config::for_profile(SimulationProfile::HbfV07);
  config.stacks = 1;
  config.host_channels_per_stack = 1;
  config.hbf_channel_count = 1;
  HbfChannelDomain channels(config);
  HbfProtocolValidator validator(config, channels);
  CHECK(validator.validate({0, OpType::Read, 0, 4096, 0}).ok());
  CHECK(validator.validate({0, OpType::Read, 0, 8192, 0}).status ==
        HbfStatus::InvalidUserField);
  CHECK(validator.validate({0, OpType::Read, 4032, 128, 0}).status ==
        HbfStatus::InvalidAddress);
  CHECK(validator.validate({0, OpType::Write, 32, 64, 0}).status ==
        HbfStatus::InvalidUserField);
  return 0;
}
