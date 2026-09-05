#pragma once

#include "hbfsim/config/config.h"
#include "hbfsim/protocol/status.h"
#include <cstdint>
#include <cstddef>
#include <deque>
#include <unordered_map>
#include <vector>

namespace hbfsim {

struct AxiEndpoint {
  std::uint32_t channel = 0;
  std::uint32_t port = 0;
  std::uint32_t id = 0;
};

class AxiOrderTracker {
 public:
  explicit AxiOrderTracker(const Config& config);
  HbfStatus issue(const AxiEndpoint& endpoint, std::uint64_t request_id);
  std::vector<HbfResponse> complete(HbfResponse response);
  std::size_t outstanding(const AxiEndpoint& endpoint) const;
  std::size_t total_outstanding() const { return owners_.size(); }

 private:
  struct EndpointKey {
    std::uint32_t channel = 0;
    std::uint32_t port = 0;
    std::uint32_t id = 0;
    friend bool operator==(const EndpointKey&, const EndpointKey&) = default;
  };
  struct EndpointHash {
    std::size_t operator()(const EndpointKey& key) const;
  };
  std::uint32_t channel_count_ = 0;
  std::uint32_t ports_per_channel_ = 0;
  std::uint32_t id_count_ = 0;
  std::uint32_t max_outstanding_per_id_ = 0;
  std::unordered_map<EndpointKey, std::deque<std::uint64_t>, EndpointHash>
      issued_;
  std::unordered_map<std::uint64_t, EndpointKey> owners_;
  std::unordered_map<std::uint64_t, HbfResponse> completed_;
};

}  // namespace hbfsim
