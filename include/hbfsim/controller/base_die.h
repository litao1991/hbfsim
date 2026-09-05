#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/controller/interconnect_model.h"
#include "hbfsim/controller/media_scheduler.h"
#include "hbfsim/media/nand_media.h"

namespace hbfsim {

class BaseDieController {
 public:
  BaseDieController(const Config& config, NandMediaSystem& media)
      : config_(config), media_(media), scheduler_(config),
        interconnect_(config) {}

  MediaScheduler& scheduler() { return scheduler_; }
  const MediaScheduler& scheduler() const { return scheduler_; }
  InterconnectModel& interconnect() { return interconnect_; }
  const InterconnectModel& interconnect() const { return interconnect_; }

  SimTime command_ready_time(const SubRequest& request) const;
  void claim_command(const SubRequest& request, SimTime now,
                     bool shared_command);

 private:
  const Config& config_;
  NandMediaSystem& media_;
  MediaScheduler scheduler_;
  InterconnectModel interconnect_;
};

}  // namespace hbfsim
