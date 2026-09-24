/// \file
/// \brief The raw layer on Linux, where the probe links with
/// `-Wl,--wrap=` for `malloc`, `calloc`, `realloc`, `free`, `aligned_alloc`,
/// `posix_memalign` and `memalign`.
///
/// The wrap redirects the calls made from this binary's own objects - which
/// include every header-only library it compiles, RapidJSON among them - so
/// they are counted like `operator new`. The counting `operator new` reaches
/// the real `malloc` through `raw_malloc`, so its blocks are counted once.
///
/// Every count is of requested bytes. A `malloc` block has no header to keep
/// its requested size in, so the wrap keeps it in a table keyed by address,
/// and a release gives back exactly what the allocation added. A `realloc`
/// releases the old block's requested size and adds the new one's, whether it
/// grew or shrank the block in place or moved it.
///
/// A block this binary's wrap did not hand out - one the C library allocated
/// internally, say, where the wrap does not reach - is not in the table.
/// Freeing it changes nothing, and reallocating it adds the new block in full.
/// Either way the counts err towards what this binary itself asked for.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>

#include "counting_allocator.hpp"

extern "C" {

auto __real_malloc(std::size_t size) -> void*;
auto __real_calloc(std::size_t count, std::size_t size) -> void*;
auto __real_realloc(void* pointer, std::size_t size) -> void*;
void __real_free(void* pointer);
auto __real_aligned_alloc(std::size_t alignment, std::size_t size) -> void*;
auto __real_posix_memalign(void** out, std::size_t alignment, std::size_t size) -> int;
auto __real_memalign(std::size_t alignment, std::size_t size) -> void*;

}  // extern "C"

namespace {

/// The requested size of every live block the wrap handed out, by address.
/// Open addressing with linear probing and backward-shift deletion, so there
/// are no tombstones and a lookup never degrades as blocks come and go. Its
/// storage comes from the real `calloc`, so the table never counts itself.
///
/// Constant-initialised and never destroyed: a `free` may arrive during
/// static initialisation or after `main` returns.
class block_sizes {
 public:
  void insert(void* pointer, std::size_t size) noexcept {
    if ((used_ + 1) * 2 > capacity_) {
      grow();
    }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    std::size_t index = home(address);
    while (slots_[index].address != 0 && slots_[index].address != address) {
      index = (index + 1) & (capacity_ - 1);
    }
    if (slots_[index].address == 0) {
      ++used_;
    }
    slots_[index] = slot{.address = address, .size = size};
  }

  /// Removes the block and returns its requested size, or nothing when the
  /// wrap never handed it out.
  auto erase(void* pointer) noexcept -> std::optional<std::size_t> {
    if (capacity_ == 0) {
      return std::nullopt;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const std::size_t mask = capacity_ - 1;
    std::size_t hole = home(address);
    while (slots_[hole].address != address) {
      if (slots_[hole].address == 0) {
        return std::nullopt;
      }
      hole = (hole + 1) & mask;
    }
    const std::size_t size = slots_[hole].size;
    // Pull back every later entry of the run that may live in the hole, so a
    // lookup's probe never stops at a gap short of its entry.
    for (std::size_t next = (hole + 1) & mask; slots_[next].address != 0;
         next = (next + 1) & mask) {
      const std::size_t wanted = home(slots_[next].address);
      const bool wanted_after_hole =
          hole <= next ? (hole < wanted && wanted <= next) : (hole < wanted || wanted <= next);
      if (!wanted_after_hole) {
        slots_[hole] = slots_[next];
        hole = next;
      }
    }
    slots_[hole] = slot{.address = 0, .size = 0};
    --used_;
    return size;
  }

 private:
  struct slot {
    std::uintptr_t address;
    std::size_t size;
  };

  static constexpr std::size_t initial_capacity = 1024;

  [[nodiscard]] auto home(std::uintptr_t address) const noexcept -> std::size_t {
    // Fibonacci hashing of the address above malloc's 16-byte alignment.
    const std::uint64_t mixed = (std::uint64_t{address} >> 4U) * 0x9E3779B97F4A7C15ULL;
    return static_cast<std::size_t>(mixed >> (64U - bits_));
  }

  void grow() noexcept {
    const std::size_t old_capacity = capacity_;
    slot* const old_slots = slots_;
    const std::size_t capacity = old_capacity == 0 ? initial_capacity : old_capacity * 2;
    auto* const fresh = static_cast<slot*>(__real_calloc(capacity, sizeof(slot)));
    if (fresh == nullptr) {
      // A table that cannot hold a block would report a count that is wrong,
      // and the probe's numbers are only worth having if they are exact.
      std::abort();
    }
    slots_ = fresh;
    capacity_ = capacity;
    bits_ = 0;
    while ((std::size_t{1} << bits_) < capacity) {
      ++bits_;
    }
    used_ = 0;
    for (std::size_t i = 0; i < old_capacity; ++i) {
      if (old_slots[i].address != 0) {
        insert(reinterpret_cast<void*>(old_slots[i].address), old_slots[i].size);
      }
    }
    __real_free(old_slots);
  }

  slot* slots_ = nullptr;
  std::size_t capacity_ = 0;
  std::size_t used_ = 0;
  unsigned bits_ = 0;
};

constinit block_sizes live_blocks{};

void track(void* pointer, std::size_t size) noexcept {
  live_blocks.insert(pointer, size);
  ce::perf::record_allocation(size);
}

void forget(void* pointer) noexcept {
  if (const auto size = live_blocks.erase(pointer)) {
    ce::perf::record_release(*size);
  }
}

}  // namespace

extern "C" {

auto __wrap_malloc(std::size_t size) -> void* {
  void* pointer = __real_malloc(size);
  if (pointer != nullptr) {
    track(pointer, size);
  }
  return pointer;
}

auto __wrap_calloc(std::size_t count, std::size_t size) -> void* {
  void* pointer = __real_calloc(count, size);
  if (pointer != nullptr) {
    // A calloc that succeeded had no overflow in count * size.
    track(pointer, count * size);
  }
  return pointer;
}

auto __wrap_realloc(void* pointer, std::size_t size) -> void* {
  void* moved = __real_realloc(pointer, size);
  if (moved != nullptr) {
    if (pointer != nullptr) {
      forget(pointer);
    }
    track(moved, size);
  } else if (pointer != nullptr && size == 0) {
    // glibc frees the block and returns null for a zero size.
    forget(pointer);
  }
  return moved;
}

void __wrap_free(void* pointer) {
  if (pointer != nullptr) {
    forget(pointer);
  }
  __real_free(pointer);
}

auto __wrap_aligned_alloc(std::size_t alignment, std::size_t size) -> void* {
  void* pointer = __real_aligned_alloc(alignment, size);
  if (pointer != nullptr) {
    track(pointer, size);
  }
  return pointer;
}

auto __wrap_posix_memalign(void** out, std::size_t alignment, std::size_t size) -> int {
  const int status = __real_posix_memalign(out, alignment, size);
  if (status == 0 && *out != nullptr) {
    track(*out, size);
  }
  return status;
}

auto __wrap_memalign(std::size_t alignment, std::size_t size) -> void* {
  void* pointer = __real_memalign(alignment, size);
  if (pointer != nullptr) {
    track(pointer, size);
  }
  return pointer;
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
