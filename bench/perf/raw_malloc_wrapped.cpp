/// \file
/// \brief The raw layer on Linux, where the probe links with
/// `-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free`.
///
/// The wrap redirects the calls made from this binary's own objects - which
/// include every header-only library it compiles, RapidJSON among them - so
/// they are counted like `operator new`. The counting `operator new` reaches
/// the real `malloc` through `raw_malloc`, so its blocks are counted once.
///
/// A `malloc` block has no header, so its live size is its usable size; the
/// requested size is still what `bytes` counts.

#include <cstddef>
#include <malloc.h>

#include "counting_allocator.hpp"

extern "C" {

auto __real_malloc(std::size_t size) -> void*;
auto __real_calloc(std::size_t count, std::size_t size) -> void*;
auto __real_realloc(void* pointer, std::size_t size) -> void*;
void __real_free(void* pointer);

auto __wrap_malloc(std::size_t size) -> void* {
  void* pointer = __real_malloc(size);
  if (pointer != nullptr) {
    ce::perf::record_allocation(size, malloc_usable_size(pointer));
  }
  return pointer;
}

auto __wrap_calloc(std::size_t count, std::size_t size) -> void* {
  void* pointer = __real_calloc(count, size);
  if (pointer != nullptr) {
    ce::perf::record_allocation(count * size, malloc_usable_size(pointer));
  }
  return pointer;
}

auto __wrap_realloc(void* pointer, std::size_t size) -> void* {
  const std::size_t before = pointer != nullptr ? malloc_usable_size(pointer) : 0;
  void* moved = __real_realloc(pointer, size);
  if (moved != nullptr) {
    ce::perf::record_release(before);
    ce::perf::record_allocation(size, malloc_usable_size(moved));
  } else if (size == 0) {
    ce::perf::record_release(before);
  }
  return moved;
}

void __wrap_free(void* pointer) {
  if (pointer != nullptr) {
    ce::perf::record_release(malloc_usable_size(pointer));
  }
  __real_free(pointer);
}

}  // extern "C"

namespace ce::perf {

auto raw_malloc(std::size_t size) noexcept -> void* {
  return __real_malloc(size);
}
void raw_free(void* pointer) noexcept {
  __real_free(pointer);
}
auto counts_malloc() noexcept -> bool {
  return true;
}

}  // namespace ce::perf
