/// \file
/// \brief The raw layer where the linker cannot wrap `malloc`: direct `malloc`
/// calls go uncounted, and the probe reports that in its output.

#include <cstddef>
#include <cstdlib>

#include "counting_allocator.hpp"

namespace ce::perf {

auto raw_malloc(std::size_t size) noexcept -> void* {
  return std::malloc(size);
}
void raw_free(void* pointer) noexcept {
  std::free(pointer);
}
auto counts_malloc() noexcept -> bool {
  return false;
}

}  // namespace ce::perf
