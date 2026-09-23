#pragma once

/// \file
/// \brief Where the committed fixtures live, supplied by CMake.

#include <string>
#include <string_view>

#ifndef CE_FIXTURE_DIR
#error "CE_FIXTURE_DIR must be defined by the build; see test/CMakeLists.txt"
#endif

namespace ce_fixtures {

inline constexpr std::string_view root = CE_FIXTURE_DIR;

[[nodiscard]] inline auto path(std::string_view relative) -> std::string {
  return std::string{root} + "/" + std::string{relative};
}

}  // namespace ce_fixtures
