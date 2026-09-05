#include "hbfsim/simulator.h"

#include <algorithm>
#include <limits>

namespace hbfsim {
void Simulator::finish_program(SubRequest& sub, SimTime now) {
  system_.media().complete_program(sub.paddr, sub.old_paddr, now);
  system_.mapper().commit_write(sub.lpn, sub.paddr, now);
}

void Simulator::complete_subrequest(std::uint64_t id, SimTime now) {
  auto sub_it = subrequests_.find(id);
  auto& sub = sub_it->second;
  complete_batch_sense(sub, now);
  sub.complete_time = now;
  const auto parent_id = sub.parent_id;
  const auto copy_job_id = sub.copy_job_id;
  const auto copy_slot = sub.copy_slot;
  const auto completed_op = sub.op;
  const auto subrequest_failed = sub.failed;
  if (is_measured(parent_id))
    stats_.record_subrequest(sub);
  auto request_it = requests_.find(parent_id);
  auto& request = request_it->second;
  request.failed = request.failed || sub.failed ||
                   !hbf_data_valid(sub.status);
  if (sub.status != HbfStatus::Success)
    request.status = sub.status;
  if (sub.failed && sub.status == HbfStatus::UncorrectableEccRetryRequired)
    request.retry_stage = sub.read_retry_stage + 1;
  const bool request_done = --request.pending_subreqs == 0;
  subrequests_.erase(sub_it);
  if (request_done) {
    request.complete_time = now;
    const auto internal = request.internal;
    const auto group = request.dlu_request_ids;
    const auto failed = request.failed;
    const auto status = request.status;
    if (!group.empty()) {
      for (const auto request_id : group) {
        auto contributor = requests_.find(request_id);
        if (contributor == requests_.end()) continue;
        contributor->second.complete_time = now;
        contributor->second.failed = failed;
        contributor->second.status = status;
        if (contributor->second.measured)
          stats_.record_request(contributor->second);
        publish_response(contributor->second);
        requests_.erase(contributor);
      }
    } else {
      if (request.measured && !internal)
        stats_.record_request(request);
      if (!internal) publish_response(request);
      requests_.erase(request_it);
    }
    if (internal && copy_job_id)
      handle_copy_completion(*copy_job_id, copy_slot, completed_op,
                             subrequest_failed, now);
  }
}

void Simulator::handle(const Event& event) {
  if (event.type == EventType::ResourceFabricStart ||
      event.type == EventType::ResourceFabricEnd ||
      event.type == EventType::ResourceHostStart ||
      event.type == EventType::ResourceHostEnd) {
    const bool start = event.type == EventType::ResourceFabricStart ||
                       event.type == EventType::ResourceHostStart;
    const bool fabric = event.type == EventType::ResourceFabricStart ||
                        event.type == EventType::ResourceFabricEnd;
    stats_.record_resource_transition(
        fabric ? ResourceKind::Fabric : ResourceKind::Host,
        static_cast<std::uint32_t>(event.request_id),
        static_cast<std::uint32_t>(event.subreq_id), start ? 1 : -1, now_);
    return;
  }
  if (event.type == EventType::DispatchWake) {
    const auto stack = static_cast<std::uint32_t>(event.subreq_id);
    auto& execution = system_.controller().execution();
    if (execution.dispatch_wake_at.at(stack) == now_)
      execution.dispatch_wake_at.at(stack) =
          std::numeric_limits<SimTime>::max();
    dispatch_ready_programs(stack, now_);
    dispatch_stack(stack, now_);
    return;
  }
  if (event.type == EventType::RefreshManagerWake) {
    if (refresh_check_at_ == now_)
      refresh_check_at_ = std::numeric_limits<SimTime>::max();
    return;
  }
  if (event.type == EventType::BatchReadEmit) {
    emit_batch_reads(static_cast<std::uint32_t>(event.subreq_id), now_);
    return;
  }
  if (event.type == EventType::DluTimeout) {
    for (const auto& expired : system_.dlu_assembler().expire(now_)) {
      bool measured = false;
      for (const auto request_id : expired.request_ids) {
        const auto request = requests_.find(request_id);
        if (request == requests_.end()) continue;
        measured = measured || request->second.measured;
        request->second.complete_time = now_;
        request->second.failed = true;
        request->second.status = expired.status;
        if (request->second.measured)
          stats_.record_request(request->second);
        publish_response(request->second);
        requests_.erase(request);
      }
      if (measured) stats_.record_dlu_timeout();
    }
    return;
  }

  auto& request = requests_.at(event.request_id);
  switch (event.type) {
    case EventType::DispatchWake:
    case EventType::RefreshManagerWake:
    case EventType::BatchReadEmit:
    case EventType::DluTimeout:
    case EventType::ResourceFabricStart:
    case EventType::ResourceFabricEnd:
    case EventType::ResourceHostStart:
    case EventType::ResourceHostEnd:
      break;
    case EventType::HostArrival: {
      const auto first =
          system_.mapper().map_read(request.logical_addr / config_.page_size);
      request.host_route = system_.host_router().route(request.logical_addr, first);
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
      auto& target = controller_plane(sub.paddr);
      if (!target.suspend_pending ||
          target.active_subrequest != sub.id)
        break;
      stop_array_tracking(sub, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      release_array(sub);
      target.active_subrequest.reset();
      target.suspend_pending = false;
      target.suspended_subrequest = sub.id;
      target.busy = false;
      system_.media().set_array_ready_at(sub.paddr, now_);
      sub.suspended = true;
      dispatch_stack(sub.paddr.stack, now_);
      break;
    }
    case EventType::NandReadDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      if (sub.array_completion_time != now_) break;
      auto& target = controller_plane(sub.paddr);
      const auto& block = media_plane(sub.paddr).blocks.at(sub.paddr.block);
      const auto result = system_.reliability().read_result(
          sub.bytes, sub.read_attempts, block.erase_count, block.read_count,
          system_.media().block_retention_age(sub.paddr, now_));
      system_.media().record_read(sub.paddr);
      const bool host_driven_retry =
          config_.host_driven_read_retry &&
          config_.simulation_profile != SimulationProfile::MediaResearch;
      if (!host_driven_retry &&
          result.status == ReadErrorStatus::Uncorrectable &&
          sub.read_attempts < config_.max_read_retries) {
        if (is_measured(sub.parent_id))
          stats_.record_read_retry();
        ++sub.read_attempts;
        const auto done = now_ + config_.read_retry_ns +
                          config_.read_ns;
        sub.array_completion_time = done;
        system_.media().set_array_ready_at(sub.paddr, done);
        schedule(done, EventType::NandReadDone, request.id, sub.id);
        break;
      }
      if (result.status == ReadErrorStatus::Corrected) {
        system_.media().clear_page_failure(sub.paddr);
        if (is_measured(sub.parent_id))
          stats_.record_corrected_read();
        if (config_.simulation_profile != SimulationProfile::MediaResearch)
          sub.status = HbfStatus::CorrectedEccRefreshRequired;
      } else if (result.status == ReadErrorStatus::Uncorrectable) {
        system_.media().mark_page_failure(sub.paddr);
        sub.failed = true;
        sub.status =
            host_driven_retry &&
                    sub.read_retry_stage >= config_.max_read_retries
                ? HbfStatus::UncorrectableEcc
                : config_.simulation_profile != SimulationProfile::MediaResearch
                      ? HbfStatus::UncorrectableEccRetryRequired
                      : HbfStatus::UncorrectableEcc;
        if (is_measured(sub.parent_id))
          stats_.record_uncorrectable_read();
      } else {
        system_.media().clear_page_failure(sub.paddr);
      }
      if (!sub.failed && system_.media().page_is_valid(sub.paddr))
        if (system_.media().read_cache_fill(sub.paddr, now_) &&
            is_measured(sub.parent_id))
          stats_.record_read_cache_eviction();
      complete_batch_sense(sub, now_);
      system_.media().clear_transient_page_state(sub.paddr);
      stop_array_tracking(sub, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      release_array(sub);
      target.active_subrequest.reset();
      system_.media().set_data_register_busy(sub.paddr, true);
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
      system_.media().set_data_register_busy(sub.paddr, false);
      sub.ready_time = now_ +
                       (config_.multi_plane_enabled
                            ? config_.multi_plane_setup_ns
                            : 0);
      die(sub.paddr).ready_at =
          std::max(die(sub.paddr).ready_at,
                   now_ + config_.t_whr_ns);
      system_.controller().execution().program_ready.at(sub.paddr.stack)
          .push_back(sub.id);
      dispatch_ready_programs(sub.paddr.stack, now_);
      dispatch_stack(sub.paddr.stack, now_);
      break;
    }
    case EventType::NandAutoEraseDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      if (sub.array_completion_time != now_) break;
      auto& target = controller_plane(sub.paddr);
      const auto& block = media_plane(sub.paddr).blocks.at(sub.paddr.block);
      stop_array_tracking(sub, now_);
      const auto erase_service = now_ - sub.array_active_since;
      sub.latency.array_service_ns += erase_service;
      sub.latency.auto_erase_service_ns += erase_service;
      release_array(sub);

      if (system_.reliability().erase_failed(block.erase_count)) {
        sub.auto_erase_failed = true;
        sub.failed = true;
        sub.status = HbfStatus::ReducedCapacity;
        if (is_measured(sub.parent_id)) stats_.record_erase_failure();
        retire_block(sub.paddr);
      } else {
        const auto erase_count = system_.media().complete_erase(sub.paddr);
        system_.mapper().on_erase(sub.paddr);
        if (config_.max_erase_cycles != 0 &&
            erase_count >= config_.max_erase_cycles) {
          sub.auto_erase_retired = true;
          sub.failed = true;
          sub.status = HbfStatus::ReducedCapacity;
          retire_block(sub.paddr);
        }
      }

      const auto stack = sub.paddr.stack;
      if (sub.failed) {
        target.active_subrequest.reset();
        target.busy = false;
        complete_subrequest(sub.id, now_);
      } else {
        const auto ready = now_ + config_.t_whr_ns;
        sub.ready_time = ready;
        system_.media().set_array_ready_at(sub.paddr, ready);
        die(sub.paddr).ready_at =
            std::max(die(sub.paddr).ready_at, ready);
        schedule(ready, EventType::NandAutoEraseProgramReady,
                 sub.parent_id, sub.id);
      }
      dispatch_ready_programs(stack, now_);
      dispatch_stack(stack, now_);
      break;
    }
    case EventType::NandAutoEraseProgramReady: {
      auto& sub = subrequests_.at(event.subreq_id);
      auto& target = controller_plane(sub.paddr);
      target.active_subrequest.reset();
      target.busy = false;
      start_program(sub.id, now_);
      break;
    }
    case EventType::NandProgramDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      if (sub.array_completion_time != now_) break;
      auto& target = controller_plane(sub.paddr);
      const auto& block = media_plane(sub.paddr).blocks.at(sub.paddr.block);
      stop_array_tracking(sub, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      std::optional<ProgramFailureNotice> failure_notice;
      if (sub.auto_erase_failed || sub.auto_erase_retired) {
        system_.media().clear_transient_page_state(sub.paddr);
        sub.failed = true;
        sub.status = sub.auto_erase_retired ? HbfStatus::ReducedCapacity
                                            : HbfStatus::EraseFailure;
        if (sub.auto_erase_failed && is_measured(sub.parent_id))
          stats_.record_erase_failure();
        retire_block(sub.paddr);
      } else if (system_.reliability().program_failed(block.erase_count)) {
        system_.media().fail_program(sub.paddr, now_);
        if (config_.mapping_policy == MappingPolicy::HostManaged) {
          failure_notice = system_.mapper().fail_write(sub.lpn, sub.paddr);
          program_failure_notices_.push_back(*failure_notice);
          system_.replay_manager().record(*failure_notice, config_.page_size);
        }
        sub.failed = true;
        sub.status =
            config_.simulation_profile != SimulationProfile::MediaResearch ||
                    config_.mapping_policy == MappingPolicy::HostManaged
                ? HbfStatus::ProgramFailureReplayRequired
                : HbfStatus::ProgramFailure;
        if (is_measured(sub.parent_id))
          stats_.record_program_failure();
      } else {
        finish_program(sub, now_);
      }
      release_array(sub);
      target.active_subrequest.reset();
      target.busy = false;
      system_.media().set_array_ready_at(sub.paddr,
                                         now_ + config_.t_whr_ns);
      die(sub.paddr).ready_at =
          std::max(die(sub.paddr).ready_at,
                   now_ + config_.t_whr_ns);
      const auto stack = sub.paddr.stack;
      const auto source = sub.source;
      const auto measured = is_measured(sub.parent_id);
      complete_subrequest(sub.id, now_);
      if (failure_notice && source == TransactionSource::User &&
          config_.auto_recovery_enabled) {
        auto& pending_recoveries = system_.copy_engine().pending_recoveries();
        const auto duplicate = std::any_of(
            pending_recoveries.begin(), pending_recoveries.end(),
            [&](const auto& pending) {
              return pending.source_stripe == failure_notice->stripe;
            });
        if (!duplicate)
          pending_recoveries.push_back(
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
      auto& target = controller_plane(sub.paddr);
      const auto& block = media_plane(sub.paddr).blocks.at(sub.paddr.block);
      stop_array_tracking(sub, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      if (system_.reliability().erase_failed(block.erase_count)) {
        sub.failed = true;
        sub.status = HbfStatus::EraseFailure;
        if (is_measured(sub.parent_id)) stats_.record_erase_failure();
        retire_block(sub.paddr);
      } else {
        const auto erase_count = system_.media().complete_erase(sub.paddr);
        if (config_.max_erase_cycles != 0 &&
            erase_count >= config_.max_erase_cycles)
          retire_block(sub.paddr);
        else
          system_.mapper().on_erase(sub.paddr);
      }
      release_array(sub);
      target.active_subrequest.reset();
      target.busy = false;
      system_.media().set_array_ready_at(sub.paddr,
                                         now_ + config_.t_whr_ns);
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
      auto& target = controller_plane(sub.paddr);
      stop_array_tracking(sub, now_);
      sub.latency.array_service_ns += now_ - sub.array_active_since;
      system_.media().mark_refreshed(sub.paddr, now_);
      release_array(sub);
      target.active_subrequest.reset();
      target.busy = false;
      system_.media().set_array_ready_at(sub.paddr,
                                         now_ + config_.t_whr_ns);
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
      auto& target = controller_plane(sub.paddr);
      system_.media().set_data_register_busy(sub.paddr, false);
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
    case EventType::ReadCacheDataOutDone: {
      auto& sub = subrequests_.at(event.subreq_id);
      const auto host = reserve_host(
          sub.host_route, HostLinkDirection::DeviceToHost, now_, sub.bytes,
          is_measured(sub.parent_id));
      sub.latency.host_data_wait_ns += host.start - now_;
      sub.latency.host_data_service_ns += host.completion - host.start;
      schedule(host.completion, EventType::SubreqDone,
               request.id, sub.id);
      break;
    }
    case EventType::SubreqDone:
      complete_subrequest(event.subreq_id, now_);
      break;
  }
}

}  // namespace hbfsim
