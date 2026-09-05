#include "internal.h"

#include <limits>
#include <stdexcept>

namespace hbfsim {

CsvTraceSource::CsvTraceSource(const std::string& path)
    : input_(path), path_(path) {
  if (!input_) throw std::runtime_error("cannot open trace: " + path);
}

bool CsvTraceSource::next(TraceEntry& entry) {
  std::string line;
  while (std::getline(input_, line)) {
    ++line_number_;
    if (detail::trim(line).empty() || detail::trim(line).starts_with('#'))
      continue;
    const auto fields = detail::split(line, ',');
    if (first_record_ && !fields.empty() &&
        detail::lower(fields[0]).find("timestamp") != std::string::npos) {
      first_record_ = false;
      continue;
    }
    first_record_ = false;
    if (fields.size() < 4)
      throw std::runtime_error(path_ + ":" + std::to_string(line_number_) +
                               ": expected timestamp,op,address,size");
    TraceEntry parsed{detail::parse_u64(fields[0]), parse_op(fields[1]),
                      detail::parse_u64(fields[2]), parse_size(fields[3]),
                      static_cast<std::uint32_t>(
                          fields.size() > 4 ? detail::parse_u64(fields[4]) : 0),
                      static_cast<std::uint32_t>(
                          fields.size() > 5 ? detail::parse_u64(fields[5]) : 0),
                      static_cast<std::uint32_t>(
                          fields.size() > 6
                              ? detail::parse_u64(fields[6])
                              : std::numeric_limits<std::uint32_t>::max())};
    if (previous_timestamp_ && parsed.timestamp_ns < *previous_timestamp_)
      throw std::runtime_error(path_ + ":" + std::to_string(line_number_) +
                               ": trace timestamps must be nondecreasing");
    previous_timestamp_ = parsed.timestamp_ns;
    entry = parsed;
    return true;
  }
  return false;
}

}  // namespace hbfsim
