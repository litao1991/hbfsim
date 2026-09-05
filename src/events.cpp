#include "hbfsim/core.h"

#include <algorithm>
#include <limits>

namespace hbfsim {
namespace {

bool bitmap_test(const std::vector<std::uint64_t>& bitmap,
                 std::uint32_t page) {
  const auto word = page / 64;
  return word < bitmap.size() &&
         (bitmap[word] & (1ULL << (page % 64))) != 0;
}

void bitmap_set(std::vector<std::uint64_t>& bitmap, std::uint32_t pages,
                std::uint32_t page) {
  if (bitmap.empty()) bitmap.resize((pages + 63) / 64, 0);
  bitmap.at(page / 64) |= 1ULL << (page % 64);
}

void bitmap_clear(std::vector<std::uint64_t>& bitmap, std::uint32_t page) {
  if (!bitmap.empty()) bitmap.at(page / 64) &= ~(1ULL << (page % 64));
}

}  // namespace

void Simulator::finish_program(SubRequest& sub, SimTime now) {
  if (sub.old_paddr) {
    auto& old_block =
        plane(*sub.old_paddr).blocks.at(sub.old_paddr->block);
    if (bitmap_test(old_block.valid_bitmap, sub.old_paddr->page)) {
      bitmap_clear(old_block.valid_bitmap, sub.old_paddr->page);
      bitmap_set(old_block.invalid_bitmap, config_.pages_per_block,
                 sub.old_paddr->page);
      --old_block.valid_pages;
      ++old_block.invalid_pages;
    }
  }
  auto& block = plane(sub.paddr).blocks.at(sub.paddr.block);
  bitmap_set(block.valid_bitmap, config_.pages_per_block, sub.paddr.page);
  bitmap_clear(block.invalid_bitmap, sub.paddr.page);
  bitmap_clear(block.failed_bitmap, sub.paddr.page);
  clear_transient_page_state(sub.paddr);
  block.state = BlockState::Open;
  ++block.next_program_page;
  ++block.valid_pages;
  block.last_program_time = now;
  if (block.next_program_page == config_.pages_per_block)
    block.state = BlockState::Closed;
  mapper_.commit_write(sub.lpn, sub.paddr);
}

void Simulator::complete_subrequest(std::uint64_t id, SimTime now) {
  auto sub_it = subrequests_.find(id);
  auto& sub = sub_it->second;
  sub.complete_time = now;
  const auto parent_id = sub.parent_id;
  const auto copy_job_id = sub.copy_job_id;
  const auto completed_op = sub.op;
  const auto subrequest_failed = sub.failed;
  if (is_measured(parent_id))
    stats_.record_subrequest(sub);
  auto request_it = requests_.find(parent_id);
  auto& request = request_it->second;
  request.failed = request.failed || sub.failed;
  const bool request_done = --request.pending_subreqs == 0;
  subrequests_.erase(sub_it);
  if (request_done) {
    request.complete_time = now;
    const auto internal = request.internal;
    if (request.measured && !internal)
      stats_.record_request(request);
    requests_.erase(request_it);
    if (internal && copy_job_id)
      handle_copy_completion(*copy_job_id, completed_op,
                             subrequest_failed, now);
  }
}

void Simulator::handle(const Event& event) {
  if (event.type == EventType::DispatchWake) {
    const auto stack = static_cast<std::uint32_t>(event.subreq_id);
    if (dispatch_wake_at_.at(stack) == now_)
      dispatch_wake_at_.at(stack) =
          std::numeric_limits<SimTime>::max();
    dispatch_ready_programs(stack, now_);
    dispatch_stack(stack, now_);
    return;
  }

  auto& request = requests_.at(event.request_id);
  switch (event.type) {
    case EventType::DispatchWake:
      break;
    case EventType::HostArrival: {
      const auto first =
          mapper_.map_read(request.logical_addr / config_.page_size);
      request.host_route = host_router_.route(request.logical_addr, first);
      const auto command = reserve_host(
          request.host_route, HostLinkDirection::Command, now_, 64,
          request.measured);
      request.host_command_wait_ns = command.start - now_;
      request.host_command_service_ns = command.completion - command.start;
      schedule(command.completion,
               EventType::HostCommandDone, request.id);
      break;
    }
    case EventType::HostCommandDone:
      split_request(request);
      break;
    case EventType::SubreqReady:
      enqueue_subrequest(subrequests_.at(event.subreq_id));
      break;
    case EventType::NandSuspendDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      auto& target = plane(sub.paddr);
      if (!target.suspend_pending ||
          target.active_subrequest != sub.id)
        break;
      if (is_measured(sub.parent_id))
        stats_.record_array_issue(sub.paddr.stack, sub.paddr.die,
                                  plane_index(sub.paddr),
                                  sub.array_active_since, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      release_array(sub);
      target.active_subrequest.reset();
      target.suspend_pending = false;
      target.suspended_subrequest = sub.id;
      target.busy = false;
      target.ready_at = now_;
      target.blocks.at(sub.paddr.block).ready_at = now_;
      sub.suspended = true;
      dispatch_stack(sub.paddr.stack, now_);
      break;
    }
    case EventType::NandReadDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      if (sub.array_completion_time != now_) break;
      auto& target = plane(sub.paddr);
      const auto result =
          reliability_.read_result(sub.bytes, sub.read_attempts);
      if (result.status == ReadErrorStatus::Uncorrectable &&
          sub.read_attempts < config_.max_read_retries) {
        if (is_measured(sub.parent_id))
          stats_.record_read_retry();
        ++sub.read_attempts;
        const auto done = now_ + config_.read_retry_ns +
                          config_.read_ns;
        sub.array_completion_time = done;
        target.ready_at = done;
        target.blocks.at(sub.paddr.block).ready_at = done;
        schedule(done, EventType::NandReadDone, request.id, sub.id);
        break;
      }
      auto& block = target.blocks.at(sub.paddr.block);
      if (result.status == ReadErrorStatus::Corrected) {
        bitmap_clear(block.failed_bitmap, sub.paddr.page);
        if (is_measured(sub.parent_id))
          stats_.record_corrected_read();
      } else if (result.status == ReadErrorStatus::Uncorrectable) {
        bitmap_set(block.failed_bitmap, config_.pages_per_block,
                   sub.paddr.page);
        sub.failed = true;
        if (is_measured(sub.parent_id))
          stats_.record_uncorrectable_read();
      } else {
        bitmap_clear(block.failed_bitmap, sub.paddr.page);
      }
      clear_transient_page_state(sub.paddr);
      if (is_measured(sub.parent_id))
        stats_.record_array_issue(sub.paddr.stack, sub.paddr.die,
                                  plane_index(sub.paddr),
                                  sub.array_active_since, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      release_array(sub);
      target.active_subrequest.reset();
      target.data_register_busy = true;
      sub.complete_time = now_;
      const auto fabric = reserve_fabric(sub.paddr, now_, sub.bytes,
                                         is_measured(sub.parent_id));
      sub.latency.fabric_wait_ns += fabric.start - now_;
      sub.latency.fabric_service_ns += fabric.completion - fabric.start;
      schedule(fabric.completion,
               EventType::NandDataOutDone, request.id, sub.id);
      dispatch_ready_programs(sub.paddr.stack, now_);
      dispatch_stack(sub.paddr.stack, now_);
      break;
    }
    case EventType::NandDataInDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      auto& target = plane(sub.paddr);
      target.data_register_busy = false;
      sub.ready_time = now_ +
                       (config_.multi_plane_enabled
                            ? config_.multi_plane_setup_ns
                            : 0);
      die(sub.paddr).ready_at =
          std::max(die(sub.paddr).ready_at,
                   now_ + config_.t_whr_ns);
      program_ready_.at(sub.paddr.stack).push_back(sub.id);
      dispatch_ready_programs(sub.paddr.stack, now_);
      dispatch_stack(sub.paddr.stack, now_);
      break;
    }
    case EventType::NandProgramDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      if (sub.array_completion_time != now_) break;
      auto& target = plane(sub.paddr);
      auto& block = target.blocks.at(sub.paddr.block);
      if (is_measured(sub.parent_id))
        stats_.record_array_issue(sub.paddr.stack, sub.paddr.die,
                                  plane_index(sub.paddr),
                                  sub.array_active_since, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      std::optional<ProgramFailureNotice> failure_notice;
      if (reliability_.program_failed()) {
        clear_transient_page_state(sub.paddr);
        bitmap_set(block.failed_bitmap, config_.pages_per_block,
                   sub.paddr.page);
        bitmap_clear(block.valid_bitmap, sub.paddr.page);
        bitmap_clear(block.invalid_bitmap, sub.paddr.page);
        ++block.next_program_page;
        block.last_program_time = now_;
        block.state = block.next_program_page == config_.pages_per_block
                          ? BlockState::Closed
                          : BlockState::Open;
        if (config_.mapping_policy == MappingPolicy::HostManaged) {
          failure_notice = mapper_.fail_write(sub.lpn, sub.paddr);
          program_failure_notices_.push_back(*failure_notice);
        }
        sub.failed = true;
        if (is_measured(sub.parent_id))
          stats_.record_program_failure();
      } else {
        finish_program(sub, now_);
      }
      release_array(sub);
      target.active_subrequest.reset();
      target.busy = false;
      target.ready_at = now_ + config_.t_whr_ns;
      block.ready_at = now_ + config_.t_whr_ns;
      die(sub.paddr).ready_at =
          std::max(die(sub.paddr).ready_at,
                   now_ + config_.t_whr_ns);
      const auto stack = sub.paddr.stack;
      const auto source = sub.source;
      const auto measured = is_measured(sub.parent_id);
      complete_subrequest(sub.id, now_);
      if (failure_notice && source == TransactionSource::User &&
          config_.auto_recovery_enabled) {
        const auto duplicate = std::any_of(
            pending_recoveries_.begin(), pending_recoveries_.end(),
            [&](const auto& pending) {
              return pending.source_stripe == failure_notice->stripe;
            });
        if (!duplicate)
          pending_recoveries_.push_back(
              {failure_notice->stripe, measured});
      }
      if (config_.auto_recovery_enabled) start_ready_recoveries(now_);
      dispatch_ready_programs(stack, now_);
      dispatch_stack(stack, now_);
      break;
    }
    case EventType::NandEraseDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      if (sub.array_completion_time != now_) break;
      auto& target = plane(sub.paddr);
      auto& block = target.blocks.at(sub.paddr.block);
      if (is_measured(sub.parent_id))
        stats_.record_array_issue(sub.paddr.stack, sub.paddr.die,
                                  plane_index(sub.paddr),
                                  sub.array_active_since, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      block.state = BlockState::Free;
      block.next_program_page = 0;
      block.valid_pages = 0;
      block.invalid_pages = 0;
      block.valid_bitmap.clear();
      block.invalid_bitmap.clear();
      block.failed_bitmap.clear();
      ++block.erase_count;
      mapper_.on_erase(sub.paddr);
      release_array(sub);
      target.active_subrequest.reset();
      target.busy = false;
      target.ready_at = now_ + config_.t_whr_ns;
      block.ready_at = now_ + config_.t_whr_ns;
      die(sub.paddr).ready_at =
          std::max(die(sub.paddr).ready_at,
                   now_ + config_.t_whr_ns);
      const auto stack = sub.paddr.stack;
      complete_subrequest(sub.id, now_);
      dispatch_ready_programs(stack, now_);
      dispatch_stack(stack, now_);
      break;
    }
    case EventType::NandRefreshDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      if (sub.array_completion_time != now_) break;
      auto& target = plane(sub.paddr);
      if (is_measured(sub.parent_id))
        stats_.record_array_issue(sub.paddr.stack, sub.paddr.die,
                                  plane_index(sub.paddr),
                                  sub.array_active_since, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      target.blocks.at(sub.paddr.block).last_refresh_time = now_;
      release_array(sub);
      target.active_subrequest.reset();
      target.busy = false;
      target.ready_at = now_ + config_.t_whr_ns;
      target.blocks.at(sub.paddr.block).ready_at =
          now_ + config_.t_whr_ns;
      die(sub.paddr).ready_at =
          std::max(die(sub.paddr).ready_at,
                   now_ + config_.t_whr_ns);
      const auto stack = sub.paddr.stack;
      complete_subrequest(sub.id, now_);
      dispatch_ready_programs(stack, now_);
      dispatch_stack(stack, now_);
      break;
    }
    case EventType::NandDataOutDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      auto& target = plane(sub.paddr);
      target.data_register_busy = false;
      target.busy = false;
      const auto host = reserve_host(
          sub.host_route, HostLinkDirection::DeviceToHost, now_, sub.bytes,
          is_measured(sub.parent_id));
      sub.latency.host_data_wait_ns += host.start - now_;
      sub.latency.host_data_service_ns += host.completion - host.start;
      schedule(host.completion,
               EventType::SubreqDone, request.id, sub.id);
      dispatch_stack(sub.paddr.stack, now_);
      break;
    }
    case EventType::SubreqDone:
      complete_subrequest(event.subreq_id, now_);
      break;
  }
}

}  // namespace hbfsim
