/// \file
/// \brief The counters every counting layer reports to. The probe is single
/// threaded, so they are plain integers.
///
/// They live apart from the counting `operator new` so the `malloc` layer can
/// be linked and tested without replacing the global allocation functions.

#include <cstddef>
#include <cstdint>

#include "counting_allocator.hpp"

namespace ce::perf {
namespace {

std::uint64_t allocation_count = 0;
std::uint64_t allocated_bytes = 0;
std::int64_t live = 0;

}  // namespace

auto counters() noexcept -> heap_counters {
  return heap_counters{
      .allocations = allocation_count, .bytes = allocated_bytes, .live_bytes = live};
}

void record_allocation(std::size_t requested) noexcept {
  ++allocation_count;
  allocated_bytes += requested;
  live += static_cast<std::int64_t>(requested);
}

void record_release(std::size_t requested) noexcept {
  live -= static_cast<std::int64_t>(requested);
}

}  // namespace ce::perf
