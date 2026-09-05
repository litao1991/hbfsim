#include "hbfsim/core.h"

#include "../test_support.h"

#include <stdexcept>

int main() {
  using namespace hbfsim;

  const auto success = HbfResponse::success(7, 120, 4096);
  CHECK(success.ok());
  CHECK(success.request_id == 7);
  CHECK(success.completion_time == 120);
  CHECK(success.bytes_completed == 4096);
  CHECK(!success.protocol_status_code.has_value());
  CHECK(!success.error.has_value());

  PhysicalAddr paddr;
  paddr.stack = 1;
  paddr.die = 2;
  HbfErrorInfo error;
  error.logical_address = 0x4000;
  error.physical_address = paddr;
  error.retry_stage = 2;
  error.retirement_granularity = RetirementGranularity::Block;
  error.reason = "program replay required";
  const auto failure = HbfResponse::failure(
      8, HbfStatus::ProgramFailureReplayRequired, error, 240);
  CHECK(!failure.ok());
  CHECK(failure.error.has_value());
  CHECK(failure.error->logical_address == 0x4000);
  CHECK(failure.error->physical_address->die == 2);
  CHECK(failure.error->retry_stage == 2);
  CHECK(to_string(failure.status) == "PROGRAM_FAILURE_REPLAY_REQUIRED");

  bool rejected_success_failure = false;
  try {
    static_cast<void>(HbfResponse::failure(9, HbfStatus::Success, error));
  } catch (const std::invalid_argument&) {
    rejected_success_failure = true;
  }
  CHECK(rejected_success_failure);
  return 0;
}
