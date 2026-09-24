#pragma once

/// \file
/// \brief The heap counters the probe reads, and the raw allocation layer the
/// counting `operator new` sits on.
///
/// The probe replaces every global `operator new` and `operator delete` in its
/// own binary, so every allocation the SDK, the standard library and a codec
/// make through them is counted. Some libraries call `malloc` directly -
/// RapidJSON's `CrtAllocator` does - and those calls are counted only where the
/// raw layer can see them: `raw_malloc_wrapped.cpp` on Linux, where the linker
/// wraps `malloc` for this binary. Elsewhere `counts_malloc()` is false and the
/// counts cover `operator new` alone.

#include <cstddef>
#include <cstdint>

namespace ce::perf {

/// A snapshot of the counters. `allocations` and `bytes` only grow;
/// `live_bytes` is what is allocated and not yet released.
struct heap_counters {
  std::uint64_t allocations;
  std::uint64_t bytes;
  std::int64_t live_bytes;
};

[[nodiscard]] auto counters() noexcept -> heap_counters;

/// Called by the counting layers for every allocation and release they see.
/// `requested` is what the caller asked for; `held` is what the live-byte
/// count moves by, which for `malloc` is the usable size of the block.
void record_allocation(std::size_t requested, std::size_t held) noexcept;
void record_release(std::size_t held) noexcept;

/// The allocator beneath the counting `operator new`. It must not be counted
/// again by the `malloc` layer, so each raw layer provides its own.
[[nodiscard]] auto raw_malloc(std::size_t size) noexcept -> void*;
void raw_free(void* pointer) noexcept;

/// Whether direct `malloc` calls are counted in this build.
[[nodiscard]] auto counts_malloc() noexcept -> bool;

}  // namespace ce::perf
