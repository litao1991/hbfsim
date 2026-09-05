#include "hbfsim/protocol/frontend.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace hbfsim {

ProtocolFrontend::ProtocolFrontend(const Config& config,
                                   const HbfChannelDomain& channels)
    : config_(config), validator_(config, channels), axi_(config),
      dlu_assembler_(config) {}

FrontendAdmission ProtocolFrontend::admit(const TraceEntry& entry,
                                           std::uint64_t request_id,
                                           bool measured) {
  const bool spec_profile =
      config_.simulation_profile != SimulationProfile::MediaResearch;
  const bool invalid_size =
      (entry.op == OpType::Read || entry.op == OpType::Write) &&
      entry.size == 0;
  const bool invalid_invalidate =
      entry.op == OpType::Invalidate &&
      (entry.size == 0 || entry.address % config_.page_size != 0 ||
       entry.size % config_.page_size != 0);
  if (!spec_profile && invalid_size)
    throw std::invalid_argument("read/write request size must be non-zero");
  if (!spec_profile && invalid_invalidate)
    throw std::invalid_argument(
        "invalidate range must be non-empty and page-aligned");

  Request request;
  request.id = request_id;
  request.arrival_time = entry.timestamp_ns;
  request.op = entry.op;
  request.read_type = entry.batch_hint ? ReadType::Batch : ReadType::Single;
  if (entry.op == OpType::Read && entry.read_retry_stage != 0)
    request.retry_stage = entry.read_retry_stage;
  request.logical_addr = entry.address;
  request.size = entry.size;
  request.stream_id = entry.stream;
  request.measured = measured;

  const auto reject = [&](HbfStatus status, const std::string& reason) {
    request.complete_time = entry.timestamp_ns;
    request.failed = true;
    request.status = status;
    HbfErrorInfo error;
    error.logical_address = entry.address;
    error.reason = reason;
    auto response = HbfResponse::failure(
        request_id, status, std::move(error), entry.timestamp_ns);
    if (spec_profile &&
        (entry.op == OpType::Read || entry.op == OpType::Write))
      response.protocol_status_code = hbf_status_code(entry.op, status);
    return FrontendAdmission{request, std::move(response)};
  };

  if (invalid_size || invalid_invalidate)
    return reject(HbfStatus::InvalidUserField,
                  invalid_size
                      ? "read/write size must be non-zero"
                      : "invalidate range must be non-empty and aligned");
  if (entry.op == OpType::Read &&
      entry.read_retry_stage > config_.max_read_retries)
    return reject(HbfStatus::InvalidUserField,
                  "read retry stage exceeds configured retry limit");

  std::optional<HbfChannelAddress> channel_address;
  if (spec_profile) {
    const auto validation = validator_.validate(entry);
    if (!validation.ok())
      return reject(validation.status, validation.reason);
    channel_address = validation.address;
  }

  if (channel_address) {
    request.axi = {
        channel_address->channel,
        entry.axi_port == std::numeric_limits<std::uint32_t>::max()
            ? channel_address->axi_port
            : entry.axi_port,
        entry.axi_id};
    const auto status = axi_.issue(request.axi, request.id);
    if (status != HbfStatus::Success)
      return reject(entry.op == OpType::Write &&
                            status == HbfStatus::TemporarilyRestricted
                        ? HbfStatus::DieTemporarilyBlocked
                        : status,
                    "AXI endpoint rejected request");
    request.axi_tracked = true;
  }
  return {std::move(request), std::nullopt};
}

std::vector<HbfResponse> ProtocolFrontend::complete(
    const Request& request) {
  HbfResponse response;
  if (request.status != HbfStatus::Success || request.failed) {
    HbfErrorInfo error;
    error.logical_address = request.logical_addr;
    error.retry_stage = request.retry_stage;
    error.reason = to_string(request.status);
    response = HbfResponse::failure(
        request.id,
        request.status == HbfStatus::Success
            ? HbfStatus::TemporarilyRestricted
            : request.status,
        std::move(error), request.complete_time,
        hbf_data_valid(request.status) ? request.size : 0);
  } else {
    response = HbfResponse::success(request.id, request.complete_time,
                                    request.size);
  }
  if (config_.simulation_profile != SimulationProfile::MediaResearch &&
      (request.op == OpType::Read || request.op == OpType::Write))
    response.protocol_status_code =
        hbf_status_code(request.op, response.status);
  if (!request.axi_tracked) return {std::move(response)};
  return axi_.complete(std::move(response));
}

}  // namespace hbfsim
