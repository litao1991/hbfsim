#include "hbfsim/extension/copy_engine.h"

#include <algorithm>

namespace hbfsim {

std::size_t CopyEngine::active_jobs(TransactionSource source) const {
  return static_cast<std::size_t>(std::count_if(
      jobs_.begin(), jobs_.end(), [&](const auto& entry) {
        return entry.second.source == source;
      }));
}

}  // namespace hbfsim
