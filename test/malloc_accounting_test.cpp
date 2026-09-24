#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>
#include <unistd.h>
#include <vector>

#include <boost/ut.hpp>

#include "counting_allocator.hpp"

// The probe's malloc layer, linked with the same --wrap options as perf_probe.
// Every count must be of requested bytes: the gates compare a pull request
// with main by exact equality, so a count that follows where a block landed
// in the heap fails them at random.

namespace {

/// Tells the optimiser the block is used, so it cannot drop a malloc and its
/// free as a pair: a call it removes never reaches the wrap.
void keep(void* pointer) {
  asm volatile("" : : "g"(pointer) : "memory");
}

struct counted {
  std::uint64_t allocations;
  std::uint64_t bytes;
  std::int64_t live;
};

[[nodiscard]] auto since(const ce::perf::heap_counters& before) -> counted {
  const auto now = ce::perf::counters();
  return counted{.allocations = now.allocations - before.allocations,
                 .bytes = now.bytes - before.bytes,
                 .live = now.live_bytes - before.live_bytes};
}

[[nodiscard]] auto address_of(const void* pointer) -> std::uintptr_t {
  return reinterpret_cast<std::uintptr_t>(pointer);
}

}  // namespace

// spec: SWR-PERF-0002
// spec: SWR-PERF-0003
const boost::ut::suite<"malloc-accounting-counts-requested-bytes"> malloc_accounting = [] {
  using namespace boost::ut;

  expect(ce::perf::counts_malloc()) << "this suite is built only where the wrap counts malloc";

  "malloc counts the size asked for, not the block's usable size"_test = [] {
    const auto before = ce::perf::counters();
    void* block = std::malloc(1);
    keep(block);
    const auto held = since(before);
    expect(held.allocations == 1U);
    expect(held.bytes == 1U);
    expect(held.live == 1) << "a one-byte request holds one byte, whatever glibc rounded it to";
    std::free(block);
    expect(since(before).live == 0);
  };

  "calloc counts count times size"_test = [] {
    const auto before = ce::perf::counters();
    void* block = std::calloc(3, 5);
    keep(block);
    expect(since(before).bytes == 15U);
    expect(since(before).live == 15);
    std::free(block);
    expect(since(before).live == 0);
  };

  "realloc of null is a malloc"_test = [] {
    const auto before = ce::perf::counters();
    void* block = std::realloc(nullptr, 40);
    keep(block);
    expect(since(before).allocations == 1U);
    expect(since(before).live == 40);
    std::free(block);
    expect(since(before).live == 0);
  };

  "a shrink gives back the old request and holds the new one"_test = [] {
    const auto before = ce::perf::counters();
    // glibc shrinks in place; a sanitizer's allocator moves the block.
    void* block = std::malloc(4000);
    keep(block);
    block = std::realloc(block, 100);
    keep(block);
    expect(fatal(block != nullptr));
    const auto held = since(before);
    expect(held.allocations == 2U);
    expect(held.bytes == 4100U);
    expect(held.live == 100);
    std::free(block);
    expect(since(before).live == 0);
  };

  "a growth that moves the block gives back the old request and holds the new one"_test = [] {
    const auto before = ce::perf::counters();
    void* block = std::malloc(16);
    keep(block);
    // RapidJSON grows its arrays and strings by realloc. Doubling from 16
    // bytes to 1 MiB, with a neighbour allocated after every step, crosses
    // glibc's mmap threshold, so at least one step moves the block.
    std::vector<void*> neighbours(17, nullptr);
    std::size_t moves = 0;
    std::size_t size = 16;
    std::size_t neighbour_bytes = 0;
    for (void*& neighbour : neighbours) {
      size *= 2;
      const auto old_address = address_of(block);
      block = std::realloc(block, size);
      keep(block);
      expect(fatal(block != nullptr));
      moves += address_of(block) != old_address ? 1U : 0U;
      expect(since(before).live == static_cast<std::int64_t>(size + neighbour_bytes))
          << "after growing to " << size;
      neighbour = std::malloc(8);
      keep(neighbour);
      std::free(neighbour);
      neighbour = std::malloc(24);
      keep(neighbour);
      neighbour_bytes += 24;
    }
    expect(moves > 0U) << "precondition: some step moved the block";
    for (void* neighbour : neighbours) {
      std::free(neighbour);
    }
    expect(since(before).live == static_cast<std::int64_t>(size));
    std::free(block);
    expect(since(before).live == 0);
  };

  "realloc to zero frees the block"_test = [] {
    const auto before = ce::perf::counters();
    void* block = std::malloc(64);
    keep(block);
    void* after = std::realloc(block, 0);
    keep(after);
    expect(since(before).live == 0);
    std::free(after);
    expect(since(before).live == 0);
  };

  "the aligned allocation functions count the size asked for"_test = [] {
    const auto before = ce::perf::counters();
    void* aligned = std::aligned_alloc(64, 128);
    keep(aligned);
    void* posix = nullptr;
    expect(fatal(posix_memalign(&posix, 64, 200) == 0));
    keep(posix);
    void* legacy = memalign(64, 300);
    keep(legacy);
    const auto held = since(before);
    expect(held.allocations == 3U);
    expect(held.bytes == 628U);
    expect(held.live == 628);
    std::free(aligned);
    std::free(posix);
    std::free(legacy);
    expect(since(before).live == 0);
  };

  "freeing a block the wrap never handed out changes nothing"_test = [] {
    const auto before = ce::perf::counters();
    // glibc allocates the buffer inside libc, where the wrap does not reach.
    char* directory = getcwd(nullptr, 0);
    keep(directory);
    expect(fatal(directory != nullptr));
    expect(since(before).live == 0);
    std::free(directory);
    expect(since(before).live == 0);
  };

  "thousands of live blocks are each given back exactly"_test = [] {
    constexpr std::size_t count = 5000;
    const auto before = ce::perf::counters();
    std::vector<void*> blocks(count, nullptr);
    std::int64_t requested = 0;
    std::size_t index = 0;
    std::ranges::generate(blocks, [&] {
      const std::size_t size = index++ % 97 + 1;
      requested += static_cast<std::int64_t>(size);
      void* block = std::malloc(size);
      keep(block);
      return block;
    });
    expect(since(before).allocations == count);
    expect(since(before).live == requested);
    // Freed in an order unrelated to the allocation order, so entries leave
    // the middle of the table's probe runs.
    constexpr std::size_t stride = 7919;
    for (std::size_t step = 0; step < count; ++step) {
      std::free(blocks[(step * stride) % count]);
    }
    expect(since(before).live == 0);
  };
};

int main() {}
