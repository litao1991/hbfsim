#pragma once

#include <stdexcept>
#include <string>

namespace hbfsim::test {

inline void check(bool condition, const char* expression, const char* file,
                  int line) {
  if (!condition)
    throw std::runtime_error(std::string(file) + ":" +
                             std::to_string(line) + ": CHECK(" + expression +
                             ") failed");
}

}  // namespace hbfsim::test

#define CHECK(expression) \
  ::hbfsim::test::check(static_cast<bool>(expression), #expression, __FILE__, \
                        __LINE__)
