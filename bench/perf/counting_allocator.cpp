/// \file
/// \brief Global `operator new` and `operator delete`, every overload, counted.
///
/// Each block carries a small header holding the offset back to the raw
/// allocation and the requested size, so the unsized and unaligned `delete`
/// overloads can still release the right number of bytes.

#include "counting_allocator.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>

namespace ce::perf {
namespace {

constexpr std::size_t header_size = 2 * sizeof(std::size_t);

auto allocate(std::size_t size, std::size_t alignment) noexcept -> void* {
  const std::size_t header = std::max(header_size, alignment);
  void* raw = raw_malloc(size + header + alignment);
  if (raw == nullptr) {
    return nullptr;
  }
  const auto base = reinterpret_cast<std::uintptr_t>(raw);
  const auto user = (base + header + alignment - 1) & ~(std::uintptr_t{alignment} - 1);
  auto* slot = reinterpret_cast<std::size_t*>(user) - 2;
  slot[0] = static_cast<std::size_t>(user - base);
  slot[1] = size;
  record_allocation(size);
  return reinterpret_cast<void*>(user);
}

void release(void* pointer) noexcept {
  if (pointer == nullptr) {
    return;
  }
  const auto* slot = static_cast<const std::size_t*>(pointer) - 2;
  const std::size_t offset = slot[0];
  record_release(slot[1]);
  raw_free(static_cast<char*>(pointer) - offset);
}

auto allocate_or_throw(std::size_t size, std::size_t alignment) -> void* {
  if (void* pointer = allocate(size, alignment)) {
    return pointer;
  }
  throw std::bad_alloc{};
}

constexpr std::size_t default_alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

}  // namespace

}  // namespace ce::perf

using ce::perf::allocate;
using ce::perf::allocate_or_throw;
using ce::perf::default_alignment;
using ce::perf::release;

auto operator new(std::size_t size) -> void* {
  return allocate_or_throw(size, default_alignment);
}
auto operator new[](std::size_t size) -> void* {
  return allocate_or_throw(size, default_alignment);
}
auto operator new(std::size_t size, const std::nothrow_t&) noexcept -> void* {
  return allocate(size, default_alignment);
}
auto operator new[](std::size_t size, const std::nothrow_t&) noexcept -> void* {
  return allocate(size, default_alignment);
}
auto operator new(std::size_t size, std::align_val_t alignment) -> void* {
  return allocate_or_throw(size, static_cast<std::size_t>(alignment));
}
auto operator new[](std::size_t size, std::align_val_t alignment) -> void* {
  return allocate_or_throw(size, static_cast<std::size_t>(alignment));
}
auto operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
    -> void* {
  return allocate(size, static_cast<std::size_t>(alignment));
}
auto operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
    -> void* {
  return allocate(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer) noexcept {
  release(pointer);
}
void operator delete[](void* pointer) noexcept {
  release(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept {
  release(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
  release(pointer);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept {
  release(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
  release(pointer);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
  release(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept {
  release(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
  release(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept {
  release(pointer);
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
  release(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
  release(pointer);
}
