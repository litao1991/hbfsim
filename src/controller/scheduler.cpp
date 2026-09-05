#include "hbfsim/simulator.h"

#include <algorithm>
#include <stdexcept>

namespace hbfsim {
namespace {

bool bitmap_test(const std::vector<std::uint64_t>& bitmap,
                 std::uint32_t page) {
  const auto word = page / 64;
  return word < bitmap.size() &&
         (bitmap[word] & (1ULL << (page % 64))) != 0;
}

}  // namespace

void Simulator::enqueue_subrequest(SubRequest& subrequest) {
  subrequest.enqueue_time = now_;
  subrequest.ready_time = now_;
  if (config_.multi_plane_enabled && subrequest.op != OpType::Write)
    subrequest.ready_time += config_.multi_plane_setup_ns;
  auto& target = controller_plane(subrequest.paddr);
  system_.controller().scheduler().enqueue(target, subrequest);
  if (subrequest.op == OpType::Read) {
    ++queue_depth_[0];
  } else if (subrequest.op == OpType::Write) {
    ++queue_depth_[1];
  } else if (subrequest.op == OpType::Erase) {
    ++queue_depth_[2];
  } else {
    ++queue_depth_[3];
  }
  record_queue_depth();
  if (subrequest.op == OpType::Read && target.busy)
    try_suspend_for_read(target, now_);
  dispatch_stack(subrequest.paddr.stack, now_);
}

std::optional<std::uint64_t> Simulator::choose_next(
    PlaneControllerState& target, SimTime now) const {
  return system_.controller().scheduler().choose(
      target, now,
      [this](std::uint64_t id) -> const SubRequest& {
        return subrequests_.at(id);
      });
}

SimTime Simulator::command_ready_time(const SubRequest& subrequest) const {
  return system_.controller().command_ready_time(subrequest);
}

void Simulator::claim_command(const SubRequest& subrequest, SimTime now,
                              bool shared_command) {
  system_.controller().claim_command(subrequest, now, shared_command);
}

bool Simulator::try_suspend_for_read(PlaneControllerState& target,
                                     SimTime now) {
  if (!config_.suspend_resume_enabled ||
      system_.controller().scheduler().queues_empty(target.reads) ||
      !target.active_subrequest || target.suspend_pending)
    return false;
  auto& active = subrequests_.at(*target.active_subrequest);
  if (media_plane(active.paddr).data_register_busy) return false;
  if (active.op != OpType::Write && active.op != OpType::Erase) return false;
  const auto next = choose_next(target, now);
  if (!next || subrequests_.at(*next).op != OpType::Read) return false;
  const auto& waiting = subrequests_.at(*next);
  if (system_.controller().scheduler().base_priority(waiting) >
          system_.controller().scheduler().base_priority(active) &&
      now - waiting.enqueue_time < config_.source_aging_ns)
    return false;
  if (active.array_completion_time <= now) return false;
  const auto command_ready =
      config_.simulation_profile == SimulationProfile::MediaResearch
          ? die(active.paddr).command_ready_at
          : bank(active.paddr).command_ready_at;
  if (command_ready > now) {
    schedule_dispatch_wake(active.paddr.stack, command_ready);
    return false;
  }
  const auto remaining = active.array_completion_time - now;
  if (remaining <= config_.suspend_ns + config_.resume_ns) return false;
  active.suspended_remaining_ns = remaining;
  active.array_completion_time = 0;
  target.suspend_pending = true;
  claim_command(active, now, false);
  schedule(now + config_.suspend_ns, EventType::NandSuspendDone,
           active.parent_id, active.id);
  return true;
}

bool Simulator::try_resume(PlaneControllerState& target, SimTime now) {
  auto& execution = system_.controller().execution();
  if (!target.suspended_subrequest || target.busy) return false;
  if (const auto next = choose_next(target, now);
      next && subrequests_.at(*next).op == OpType::Read)
    return false;
  auto& sub = subrequests_.at(*target.suspended_subrequest);
  const auto ready = command_ready_time(sub);
  if (ready > now) {
    schedule_dispatch_wake(sub.paddr.stack, ready);
    return false;
  }
  const auto die_index = static_cast<std::size_t>(sub.paddr.stack) *
                             config_.dies_per_stack +
                         sub.paddr.die;
  if (execution.active_per_stack.at(sub.paddr.stack) >=
          config_.max_active_planes_per_stack ||
      execution.active_per_die.at(die_index) >=
          config_.max_active_planes_per_die)
    return false;

  target.suspended_subrequest.reset();
  target.active_subrequest = sub.id;
  target.busy = true;
  target.consecutive_reads = 0;
  sub.suspended = false;
  sub.array_active_since = now;
  ++execution.active_per_die.at(die_index);
  ++execution.active_per_stack.at(sub.paddr.stack);
  start_array_tracking(sub, now);
  record_queue_depth();
  claim_command(sub, now, false);
  const auto done = now + config_.resume_ns + sub.suspended_remaining_ns;
  sub.array_completion_time = done;
  system_.media().set_array_ready_at(sub.paddr, done);
  const auto type = sub.op == OpType::Write
                        ? EventType::NandProgramDone
                        : EventType::NandEraseDone;
  schedule(done, type, sub.parent_id, sub.id);
  return true;
}

bool Simulator::try_issue_cached_write(PlaneControllerState& target,
                                       SimTime now) {
  if (!config_.cache_program_enabled || target.cached_write ||
      system_.controller().scheduler().queues_empty(target.writes) ||
      !target.active_subrequest)
    return false;
  const auto& active = subrequests_.at(*target.active_subrequest);
  if (media_plane(active.paddr).data_register_busy) return false;
  if (active.op != OpType::Write || active.suspended) return false;
  const auto scheduled = choose_next(target, now);
  if (!scheduled) return false;
  auto& next = subrequests_.at(*scheduled);
  if (next.op != OpType::Write) return false;
  const auto command_ready =
      config_.simulation_profile == SimulationProfile::MediaResearch
          ? die(next.paddr).command_ready_at
          : bank(next.paddr).command_ready_at;
  const auto ready =
      std::max({next.ready_time, die(next.paddr).ready_at, command_ready});
  if (ready > now) {
    schedule_dispatch_wake(next.paddr.stack, ready);
    return false;
  }
  system_.controller().scheduler().dequeue(target, next, false);
  --queue_depth_[1];
  record_queue_depth();
  target.cached_write = next.id;
  next.issue_time = now;
  next.latency.nand_queue_wait_ns += now - next.enqueue_time;
  claim_command(next, now, false);
  begin_data_in(next.id, now, true);
  return true;
}

void Simulator::dispatch_stack(std::uint32_t stack, SimTime now) {
  auto& execution = system_.controller().execution();
  const auto planes_per_stack =
      static_cast<std::uint64_t>(config_.dies_per_stack) *
      config_.planes_per_die;
  bool progress = true;
  while (progress) {
    progress = false;
    const auto cursor = execution.dispatch_cursor_per_stack.at(stack);
    for (std::uint64_t step = 0; step < planes_per_stack; ++step) {
      const auto local = (cursor + step) % planes_per_stack;
      const auto die_id = local / config_.planes_per_die;
      const auto plane_id = local % config_.planes_per_die;
      const PhysicalAddr address{stack, static_cast<std::uint32_t>(die_id),
                                 static_cast<std::uint32_t>(plane_id)};
      auto& candidate = controller_plane(address);
      if (candidate.busy) {
        if (try_suspend_for_read(candidate, now) ||
            try_issue_cached_write(candidate, now))
          progress = true;
        continue;
      }

      std::optional<std::uint64_t> next;
      if (candidate.suspended_subrequest) {
        next = choose_next(candidate, now);
        if (!next || subrequests_.at(*next).op != OpType::Read) {
          if (try_resume(candidate, now)) progress = true;
          continue;
        }
      } else {
        next = choose_next(candidate, now);
      }
      if (!next) continue;
      const auto& selected = subrequests_.at(*next);
      const auto ready = command_ready_time(selected);
      if (ready > now) {
        schedule_dispatch_wake(stack, ready);
        continue;
      }
      const bool needs_array_now = selected.op != OpType::Write;
      const auto die_index = static_cast<std::size_t>(stack) *
                                 config_.dies_per_stack +
                             die_id;
      if (needs_array_now &&
          (execution.active_per_stack.at(stack) >=
               config_.max_active_planes_per_stack ||
           execution.active_per_die.at(die_index) >=
               config_.max_active_planes_per_die))
        continue;

      const auto batch_op = selected.op;
      const auto batch_address = selected.paddr;
      issue(*next, now, false);
      std::uint32_t issued = 1;
      if (config_.multi_plane_enabled && batch_op != OpType::Write) {
        for (std::uint32_t peer_plane = 0;
             peer_plane < config_.planes_per_die &&
             issued < config_.max_multi_plane_width;
             ++peer_plane) {
          if (peer_plane == batch_address.plane) continue;
          PhysicalAddr peer_address{stack, batch_address.die, peer_plane};
          auto& peer = controller_plane(peer_address);
          if (peer.busy || peer.suspended_subrequest) continue;
          const auto peer_next = choose_next(peer, now);
          if (!peer_next) continue;
          const auto& peer_sub = subrequests_.at(*peer_next);
          const auto& peer_media = media_plane(peer_sub.paddr);
          const auto& peer_block = peer_media.blocks.at(peer_sub.paddr.block);
          const auto peer_ready =
              std::max({peer_sub.ready_time, peer_media.ready_at,
                        peer_block.ready_at, die(peer_sub.paddr).ready_at});
          if (peer_sub.op != batch_op ||
              peer_sub.paddr.block != batch_address.block ||
              peer_sub.paddr.page != batch_address.page ||
              peer_ready > now ||
              execution.active_per_stack.at(stack) >=
                  config_.max_active_planes_per_stack ||
              execution.active_per_die.at(die_index) >=
                  config_.max_active_planes_per_die)
            continue;
          issue(*peer_next, now, true);
          ++issued;
        }
      }
      execution.dispatch_cursor_per_stack.at(stack) =
          static_cast<std::uint32_t>((local + 1) % planes_per_stack);
      progress = true;
      break;
    }
  }
}

void Simulator::release_array(const SubRequest& subrequest) {
  auto& execution = system_.controller().execution();
  const auto die_index = static_cast<std::size_t>(subrequest.paddr.stack) *
                             config_.dies_per_stack +
                         subrequest.paddr.die;
  --execution.active_per_die.at(die_index);
  --execution.active_per_stack.at(subrequest.paddr.stack);
  record_queue_depth();
}

void Simulator::begin_data_in(std::uint64_t id, SimTime now, bool cached) {
  auto& sub = subrequests_.at(id);
  system_.media().set_data_register_busy(sub.paddr, true);
  const auto setup = config_.t_adl_ns +
                     (cached ? config_.cache_program_setup_ns : 0);
  const auto fabric = reserve_fabric(sub.paddr, now + setup, sub.bytes,
                                     is_measured(sub.parent_id));
  sub.latency.fabric_wait_ns += fabric.start - now;
  sub.latency.fabric_service_ns += fabric.completion - fabric.start;
  schedule(fabric.completion,
           EventType::NandDataInDone, sub.parent_id, sub.id);
}

void Simulator::issue(std::uint64_t id, SimTime now, bool shared_command) {
  auto& execution = system_.controller().execution();
  auto& sub = subrequests_.at(id);
  if (sub.op != OpType::Write && !system_.mapper().validate_generation(sub.paddr))
    throw std::runtime_error("STALE_GENERATION");
  auto& target = controller_plane(sub.paddr);
  system_.controller().scheduler().dequeue(target, sub);
  if (sub.op == OpType::Read) {
    --queue_depth_[0];
  } else if (sub.op == OpType::Write) {
    --queue_depth_[1];
  } else if (sub.op == OpType::Erase) {
    --queue_depth_[2];
  } else {
    --queue_depth_[3];
  }
  record_queue_depth();
  target.busy = true;
  sub.issue_time = now;
  sub.latency.nand_queue_wait_ns += now - sub.enqueue_time;
  claim_command(sub, now, shared_command);

  if (sub.op == OpType::Write) {
    begin_data_in(sub.id, now, false);
    return;
  }

  const auto& block = media_plane(sub.paddr).blocks.at(sub.paddr.block);
  if (sub.op == OpType::Read) {
    if (config_.strict_media_validation &&
        (block.state == BlockState::Free || block.state == BlockState::Bad ||
         !bitmap_test(block.valid_bitmap, sub.paddr.page))) {
      if (config_.simulation_profile != SimulationProfile::MediaResearch) {
        sub.failed = true;
        sub.status = HbfStatus::ErasedPageRead;
        target.busy = false;
        schedule(now, EventType::SubreqDone, sub.parent_id, sub.id);
        return;
      }
      throw std::runtime_error("READ_INVALID_PAGE at plane " +
                               std::to_string(plane_index(sub.paddr)) +
                               ", block " + std::to_string(sub.paddr.block) +
                               ", page " + std::to_string(sub.paddr.page) +
                               ", block_state " +
                               std::to_string(static_cast<int>(block.state)) +
                               ", valid " +
                               std::to_string(bitmap_test(
                                   block.valid_bitmap, sub.paddr.page)));
    }
    system_.media().begin_read(sub.paddr);
  } else if (sub.op == OpType::Erase) {
    if (block.bad || block.state == BlockState::Bad ||
        block.state == BlockState::Erasing) {
      if (config_.simulation_profile != SimulationProfile::MediaResearch) {
        sub.failed = true;
        sub.status = block.bad || block.state == BlockState::Bad
                         ? HbfStatus::ReducedCapacity
                         : HbfStatus::DieTemporarilyBlocked;
        target.busy = false;
        schedule(now, EventType::SubreqDone, sub.parent_id, sub.id);
        return;
      }
      throw std::runtime_error("ERASE_ON_BAD_OR_ERASING_BLOCK");
    }
    system_.media().begin_erase(sub.paddr);
  } else if (block.bad || block.state == BlockState::Bad ||
             block.state == BlockState::Erasing) {
    throw std::runtime_error("REFRESH_ON_BAD_OR_ERASING_BLOCK");
  }

  const auto die_index = static_cast<std::size_t>(sub.paddr.stack) *
                             config_.dies_per_stack +
                         sub.paddr.die;
  ++execution.active_per_die.at(die_index);
  ++execution.active_per_stack.at(sub.paddr.stack);
  start_array_tracking(sub, now);
  record_queue_depth();
  target.active_subrequest = sub.id;
  sub.array_active_since = now;
  SimTime latency = config_.read_ns;
  EventType completion = EventType::NandReadDone;
  if (sub.op == OpType::Erase) {
    latency = config_.erase_ns;
    completion = EventType::NandEraseDone;
  } else if (sub.op == OpType::Refresh) {
    latency = config_.read_ns + config_.program_ns;
    completion = EventType::NandRefreshDone;
  }
  const auto done = now + latency;
  sub.array_completion_time = done;
  system_.media().set_array_ready_at(sub.paddr, done);
  schedule(done, completion, sub.parent_id, sub.id);
}

void Simulator::start_program(std::uint64_t id, SimTime now,
                              bool shared_command) {
  auto& execution = system_.controller().execution();
  auto& sub = subrequests_.at(id);
  if (now > sub.ready_time)
    sub.latency.nand_command_wait_ns += now - sub.ready_time;
  if (sub.source == TransactionSource::User)
    sub.old_paddr = system_.mapper().lookup(sub.lpn);
  if (!system_.mapper().validate_generation(sub.paddr))
    throw std::runtime_error("STALE_GENERATION");
  auto& target = controller_plane(sub.paddr);
  const auto& media = media_plane(sub.paddr);
  auto& block = media.blocks.at(sub.paddr.block);
  if (target.cached_write == sub.id) target.cached_write.reset();
  if (block.bad || block.state == BlockState::Bad ||
      block.state == BlockState::Erasing) {
    if (config_.simulation_profile != SimulationProfile::MediaResearch) {
      sub.failed = true;
      sub.status = block.bad || block.state == BlockState::Bad
                       ? HbfStatus::ReducedCapacity
                       : HbfStatus::DieTemporarilyBlocked;
      target.busy = false;
      schedule(now, EventType::SubreqDone, sub.parent_id, sub.id);
      return;
    }
    throw std::runtime_error("PROGRAM_ON_BAD_OR_ERASING_BLOCK");
  }

  const bool auto_erase =
      config_.simulation_profile != SimulationProfile::MediaResearch &&
      config_.page0_auto_erase && sub.paddr.page == 0 &&
      block.state != BlockState::Free && system_.mapper().stripe_mapping() == nullptr;
  if (auto_erase) {
    sub.page0_auto_erase = true;
    system_.media().begin_erase(sub.paddr);
    target.busy = true;
    target.active_subrequest = sub.id;
    claim_command(sub, now, shared_command);
    const auto die_index = static_cast<std::size_t>(sub.paddr.stack) *
                               config_.dies_per_stack +
                           sub.paddr.die;
    ++execution.active_per_die.at(die_index);
    ++execution.active_per_stack.at(sub.paddr.stack);
    start_array_tracking(sub, now);
    record_queue_depth();
    sub.array_active_since = now;
    const auto done = now + config_.erase_ns;
    sub.array_completion_time = done;
    system_.media().set_array_ready_at(sub.paddr, done);
    schedule(done, EventType::NandAutoEraseDone, sub.parent_id, sub.id);
    if (is_measured(sub.parent_id)) stats_.record_page0_auto_erase();
    return;
  }

  if (!sub.auto_erase_failed && !sub.auto_erase_retired &&
      block.next_program_page != sub.paddr.page) {
    if (config_.simulation_profile != SimulationProfile::MediaResearch) {
      sub.failed = true;
      sub.status = HbfStatus::WriteOrderViolation;
      target.busy = false;
      schedule(now, EventType::SubreqDone, sub.parent_id, sub.id);
      return;
    }
    throw std::runtime_error("WRITE_ORDER_VIOLATION at plane " +
                             std::to_string(plane_index(sub.paddr)));
  }
  if (!sub.auto_erase_failed && !sub.auto_erase_retired &&
      bitmap_test(block.valid_bitmap, sub.paddr.page)) {
    if (config_.simulation_profile != SimulationProfile::MediaResearch) {
      sub.failed = true;
      sub.status = HbfStatus::OverlappingAddress;
      target.busy = false;
      schedule(now, EventType::SubreqDone, sub.parent_id, sub.id);
      return;
    }
    throw std::runtime_error("IN_PLACE_PROGRAM_NOT_ALLOWED at plane " +
                             std::to_string(plane_index(sub.paddr)));
  }

  target.busy = true;
  target.active_subrequest = sub.id;
  system_.media().begin_program(sub.paddr);
  claim_command(sub, now, shared_command);
  const auto die_index = static_cast<std::size_t>(sub.paddr.stack) *
                             config_.dies_per_stack +
                         sub.paddr.die;
  ++execution.active_per_die.at(die_index);
  ++execution.active_per_stack.at(sub.paddr.stack);
  start_array_tracking(sub, now);
  record_queue_depth();
  sub.array_active_since = now;
  const auto done = now + config_.program_ns;
  sub.array_completion_time = done;
  system_.media().set_array_ready_at(sub.paddr, done);
  schedule(done, EventType::NandProgramDone, sub.parent_id, sub.id);
}

void Simulator::dispatch_ready_programs(std::uint32_t stack, SimTime now) {
  auto& execution = system_.controller().execution();
  auto& ready_queue = execution.program_ready.at(stack);
  bool progress = true;
  while (progress && !ready_queue.empty() &&
         execution.active_per_stack.at(stack) <
             config_.max_active_planes_per_stack) {
    progress = false;
    for (auto it = ready_queue.begin(); it != ready_queue.end(); ++it) {
      auto& sub = subrequests_.at(*it);
      auto& target = controller_plane(sub.paddr);
      const auto& media = media_plane(sub.paddr);
      if (target.active_subrequest || target.suspended_subrequest ||
          media.data_register_busy)
        continue;
      const auto ready = command_ready_time(sub);
      if (ready > now) {
        schedule_dispatch_wake(stack, ready);
        continue;
      }
      const auto die_index = static_cast<std::size_t>(stack) *
                                 config_.dies_per_stack +
                             sub.paddr.die;
      if (execution.active_per_die.at(die_index) >=
          config_.max_active_planes_per_die)
        continue;

      const auto first_id = *it;
      const auto first_address = sub.paddr;
      ready_queue.erase(it);
      start_program(first_id, now, false);
      std::uint32_t issued = 1;
      if (config_.multi_plane_enabled) {
        for (auto peer = ready_queue.begin();
             peer != ready_queue.end() &&
             issued < config_.max_multi_plane_width;) {
          auto& peer_sub = subrequests_.at(*peer);
          auto& peer_plane = controller_plane(peer_sub.paddr);
          const auto& peer_media = media_plane(peer_sub.paddr);
          auto& peer_block =
              peer_media.blocks.at(peer_sub.paddr.block);
          const auto peer_ready = std::max(
              {peer_sub.ready_time, peer_media.ready_at,
               peer_block.ready_at, die(peer_sub.paddr).ready_at});
          if (peer_sub.paddr.die != first_address.die ||
              peer_sub.paddr.plane == first_address.plane ||
              peer_sub.paddr.block != first_address.block ||
              peer_sub.paddr.page != first_address.page ||
              peer_plane.active_subrequest ||
              peer_plane.suspended_subrequest ||
              peer_media.data_register_busy || peer_ready > now ||
              execution.active_per_stack.at(stack) >=
                  config_.max_active_planes_per_stack ||
              execution.active_per_die.at(die_index) >=
                  config_.max_active_planes_per_die) {
            ++peer;
            continue;
          }
          const auto peer_id = *peer;
          peer = ready_queue.erase(peer);
          start_program(peer_id, now, true);
          ++issued;
        }
      }
      progress = true;
      break;
    }
  }
}

}  // namespace hbfsim
