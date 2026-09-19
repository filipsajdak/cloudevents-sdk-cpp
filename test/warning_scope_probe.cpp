// This file MUST NOT compile. test/CMakeLists.txt asserts that it fails.
//
// It includes CTRE, whose headers are deliberately consumed as SYSTEM headers so
// their diagnostics do not fail our build, and then performs a narrowing
// conversion of its own. If this ever compiles, the SYSTEM marking has leaked
// onto our own include paths and the project's warning set has quietly stopped
// applying to the code it exists to guard.

#include <string_view>

#include <ctre.hpp>

namespace {
inline constexpr auto pattern = ctll::fixed_string{R"(^[a-z]+$)"};
}  // namespace

int main() {
  // CTRE must compile cleanly here despite its own diagnostics.
  const bool matched = static_cast<bool>(ctre::match<pattern>(std::string_view{"abc"}));

  // ...while this narrowing conversion in OUR code must still be an error.
  const int wide = 300;
  const char narrowed = wide;  // -Wconversion
  return matched ? narrowed : 0;
}
