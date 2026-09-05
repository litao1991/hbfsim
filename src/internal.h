#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

namespace hbfsim::detail {

inline std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

inline std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

inline std::vector<std::string> split(const std::string& line, char delimiter) {
  std::vector<std::string> fields;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, delimiter)) fields.push_back(trim(field));
  return fields;
}

inline std::uint64_t parse_u64(const std::string& value) {
  const auto normalized = trim(value);
  std::size_t used = 0;
  const auto result = std::stoull(normalized, &used, 0);
  if (used != normalized.size()) throw std::runtime_error("invalid integer: " + value);
  return result;
}

inline std::string unquote(std::string value) {
  value = trim(value);
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\'')))
    return value.substr(1, value.size() - 2);
  return value;
}

template <typename T, typename Parser>
inline void assign_if(const std::map<std::string, std::string>& values, const std::string& key,
                      T& destination, Parser parse) {
  if (const auto it = values.find(key); it != values.end())
    destination = static_cast<T>(parse(it->second));
}

}  // namespace hbfsim::detail
