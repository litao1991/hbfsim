#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/controller/interconnect.h"
#include "hbfsim/controller/base_die.h"
#include "hbfsim/frontend/trace.h"
#include "hbfsim/extension/copy_engine.h"
#include "hbfsim/hbf_system.h"
#include "hbfsim/kernel/event.h"
#include "hbfsim/media/state.h"
#include "hbfsim/media/nand_media.h"
#include "hbfsim/protocol/request.h"
#include "hbfsim/stats/stats.h"
#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hbfsim {

class Simulator {
 public:
  explicit Simulator(Config config);
  void submit(const TraceEntry& entry);
  void run();
  void run(IRequestSource& source);
  void run_until(SimTime until);
  const StatsCollector& stats() const { return stats_; }
  HbfSystem& system() { return system_; }
  const HbfSystem& system() const { return system_; }
  const std::vector<HbfResponse>& responses() const { return responses_; }
  SimTime now() const { return now_; }
  SimulationPhase phase() const { return phase_; }
  PageState page_state(const PhysicalAddr& paddr) const;
  BlockState block_state(const PhysicalAddr& paddr) const;
  SimTime block_ready_at(const PhysicalAddr& paddr) const;
  std::uint32_t block_erase_count(const PhysicalAddr& paddr) const;
  SimTime die_ready_at(const PhysicalAddr& paddr) const;
  const std::vector<ProgramFailureNotice>& program_failure_notices() const {
    return program_failure_notices_;
  }
  std::uint64_t start_host_gc(std::uint64_t logical_addr);
  std::uint64_t start_refresh(std::uint64_t logical_addr);
  void invalidate_host_page(std::uint64_t logical_addr);
  std::size_t active_copy_jobs() const { return system_.copy_engine().size(); }

 private:
  void schedule(SimTime when, EventType type, std::uint64_t request_id,
                std::uint64_t subreq_id = 0);
  void handle(const Event& event);
  void split_request(Request& request);
  void enqueue_subrequest(SubRequest& subrequest);
  void dispatch_stack(std::uint32_t stack, SimTime now);
  void schedule_dispatch_wake(std::uint32_t stack, SimTime when);
  void hold_batch_read(SubRequest& subrequest, SimTime now);
  void emit_batch_reads(std::uint32_t bank, SimTime now);
  void release_next_batch_read(std::uint32_t bank, SimTime now);
  void complete_batch_sense(SubRequest& subrequest, SimTime now);
  std::optional<std::uint64_t> choose_next(
      PlaneControllerState& plane, SimTime now) const;
  void issue(std::uint64_t subrequest_id, SimTime now,
             bool shared_command = false);
  void begin_data_in(std::uint64_t subrequest_id, SimTime now,
                     bool cached);
  void start_program(std::uint64_t subrequest_id, SimTime now,
                     bool shared_command = false);
  void dispatch_ready_programs(std::uint32_t stack, SimTime now);
  bool try_issue_cached_write(PlaneControllerState& plane, SimTime now);
  bool try_suspend_for_read(PlaneControllerState& plane, SimTime now);
  bool try_resume(PlaneControllerState& plane, SimTime now);
  void release_array(const SubRequest& subrequest);
  void finish_program(SubRequest& subrequest, SimTime now);
  LinkResource::Reservation reserve_host(const HostRoute& route,
                                         HostLinkDirection direction,
                                         SimTime now, std::uint64_t bytes,
                                         bool measured);
  LinkResource::Reservation reserve_fabric(const PhysicalAddr& paddr,
                                           SimTime now, std::uint64_t bytes,
                                           bool measured);
  void complete_subrequest(std::uint64_t subrequest_id, SimTime now);
  void publish_response(const Request& request);
  std::uint32_t plane_index(const PhysicalAddr& paddr) const;
  const PlaneMediaState& media_plane(const PhysicalAddr& paddr) const;
  PlaneControllerState& controller_plane(const PhysicalAddr& paddr);
  const PlaneControllerState& controller_plane(
      const PhysicalAddr& paddr) const;
  DieState& die(const PhysicalAddr& paddr);
  const DieState& die(const PhysicalAddr& paddr) const;
  BankState& bank(const PhysicalAddr& paddr);
  const BankState& bank(const PhysicalAddr& paddr) const;
  SimTime command_ready_time(const SubRequest& subrequest) const;
  void claim_command(const SubRequest& subrequest, SimTime now,
                     bool shared_command);
  void retire_block(const PhysicalAddr& paddr);
  bool is_measured(std::uint64_t request_id) const;
  void record_queue_depth();
  void start_array_tracking(const SubRequest& subrequest, SimTime now);
  void stop_array_tracking(const SubRequest& subrequest, SimTime now);
  std::uint64_t start_copy_job(TransactionSource source,
                               const StripeId& stripe,
                               std::optional<std::uint32_t> replay_slot,
                               bool measured, SimTime now);
  std::uint64_t start_gc_job(const StripeId& stripe, bool measured,
                             SimTime now);
  std::uint64_t start_gc_erase_only(const StripeId& stripe, bool measured,
                                    SimTime now);
  void maybe_start_host_gc(SimTime now);
  void maybe_start_automatic_refresh(SimTime now);
  void schedule_refresh_check(SimTime when);
  std::size_t active_copy_jobs(TransactionSource source) const;
  bool has_refresh_horizon(SimTime when) const;
  void advance_copy_job(std::uint64_t job_id, SimTime now);
  void handle_copy_completion(std::uint64_t job_id,
                              std::optional<std::uint32_t> slot,
                              OpType op, bool failed, SimTime now);
  void enqueue_copy_read(std::uint64_t job_id, std::uint32_t slot,
                         SimTime now);
  void enqueue_copy_program(std::uint64_t job_id, std::uint32_t slot,
                            SimTime now);
  void reserve_copy_hole(CopyJob& job, std::uint32_t slot);
  void enqueue_copy_erases(std::uint64_t job_id, SimTime now);
  void enqueue_stripe_erases(const StripeId& stripe,
                             TransactionSource source, bool measured,
                             std::optional<std::uint64_t> copy_job_id,
                             SimTime now);
  void restart_copy_job(std::uint64_t job_id, SimTime now);
  void finish_copy_job(std::uint64_t job_id, SimTime now, bool failed);
  void start_ready_recoveries(SimTime now);
  void reset_copy_attempt(CopyJob& job);
  void handle_copy_failure_drain(std::uint64_t job_id, SimTime now);

  Config config_;
  HbfSystem system_;
  SimTime now_ = 0;
  std::uint64_t next_request_id_ = 0;
  std::uint64_t next_subrequest_id_ = 0;
  std::uint64_t submitted_requests_ = 0;
  EventQueue event_queue_;
  SimTime refresh_check_at_ = std::numeric_limits<SimTime>::max();
  std::optional<SimTime> next_trace_arrival_;
  std::unordered_map<std::uint64_t, Request> requests_;
  std::unordered_map<std::uint64_t, SubRequest> subrequests_;
  struct BatchReadBucket {
    std::deque<std::uint64_t> pending;
    PhysicalAddr address;
    bool has_address = false;
    SimTime emit_at = std::numeric_limits<SimTime>::max();
  };
  std::unordered_map<std::uint32_t, BatchReadBucket> batch_reads_;
  std::vector<ProgramFailureNotice> program_failure_notices_;
  std::vector<HbfResponse> responses_;
  std::array<std::uint64_t, 4> queue_depth_{};
  SimulationPhase phase_ = SimulationPhase::Initialize;
  bool streaming_submission_ = false;
  StatsCollector stats_;
};

}  // namespace hbfsim
