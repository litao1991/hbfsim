#pragma once

#include "hbfsim/protocol/request.h"
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>

namespace hbfsim {

class IRequestSource {
 public:
  virtual ~IRequestSource() = default;
  virtual bool next(TraceEntry& entry) = 0;
};

class CsvTraceSource final : public IRequestSource {
 public:
  explicit CsvTraceSource(const std::string& path);
  bool next(TraceEntry& entry) override;

 private:
  std::ifstream input_;
  std::string path_;
  std::uint64_t line_number_ = 0;
  bool first_record_ = true;
  std::optional<SimTime> previous_timestamp_;
};

}  // namespace hbfsim
