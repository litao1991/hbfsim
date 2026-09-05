#include "internal.h"

#include <fstream>

namespace hbfsim {

std::vector<TraceEntry> TraceReader::read_csv(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open trace: " + path);
  std::vector<TraceEntry> entries;
  std::string line;
  bool first = true;
  while (std::getline(input, line)) {
    if (detail::trim(line).empty() || detail::trim(line).starts_with('#')) continue;
    const auto fields = detail::split(line, ',');
    if (first && !fields.empty() && detail::lower(fields[0]).find("timestamp") != std::string::npos) {
      first = false;
      continue;
    }
    first = false;
    if (fields.size() < 4) throw std::runtime_error("trace line needs timestamp,op,address,size: " + line);
    entries.push_back({detail::parse_u64(fields[0]), parse_op(fields[1]), detail::parse_u64(fields[2]),
                       parse_size(fields[3]), static_cast<std::uint32_t>(fields.size() > 4 ? detail::parse_u64(fields[4]) : 0)});
  }
  return entries;
}

}  // namespace hbfsim
