// This file MUST NOT compile; test/CMakeLists.txt asserts that it fails.
// If it ever does, the SYSTEM marking on the dependencies has leaked onto our own
// include paths and our warnings have stopped being errors.

#include <string_view>

#include <ctre.hpp>

namespace {
inline constexpr auto pattern = ctll::fixed_string{R"(^[a-z]+$)"};
}  // namespace

int main() {
  const bool matched = static_cast<bool>(ctre::match<pattern>(std::string_view{"abc"}));

  // This narrowing conversion must still be an error.
  const int wide = 300;
  const char narrowed = wide;  // -Wconversion
  return matched ? narrowed : 0;
}
