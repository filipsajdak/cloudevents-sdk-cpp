/// \file
/// \brief Deterministic measures of the SDK's event operations, per codec.
///
/// The performance job (ADR-0011) gates on what does not depend on the
/// machine: instructions executed, heap allocations made, and heap bytes a
/// decoded event keeps alive. This binary takes each of those for one
/// operation at a time and prints it as JSON; `measure.py` drives it.
///
///   perf_probe list                  every operation id, and which retain
///   perf_probe alloc <id>            allocations and bytes of one operation
///   perf_probe retained <id>         heap bytes a decoded event holds
///   perf_probe instr <id> <n>        run <id> n times in ce_perf_measured_loop
///
/// `instr` counts nothing itself. It is run, in the `perf_probe_instr` build
/// that has no allocation accounting linked in, under
///   valgrind --tool=callgrind --collect-atstart=no
///            --toggle-collect=ce_perf_measured_loop
/// so only the loop is counted, and instructions per operation are the loop's
/// total divided by n.
///
/// The operations are those `codec_bench.cpp` times, over the same
/// documents, so a wall-time number and a count describe the same work.

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>

#include "codecs/boost_json_codec.hpp"
#include "codecs/glaze_codec.hpp"
#include "codecs/rapidjson_codec.hpp"
#include "counting_allocator.hpp"
#include "documents.hpp"

/// The measured loop. `extern "C"` so its symbol is its name and Callgrind's
/// `--toggle-collect` can match it without a mangled signature, and never
/// inlined so there is a call to toggle on.
extern "C" [[gnu::noinline]] void ce_perf_measured_loop(bool (*run)(), std::uint64_t iterations);

extern "C" [[gnu::noinline]] void ce_perf_measured_loop(bool (*run)(), std::uint64_t iterations) {
  for (std::uint64_t i = 0; i < iterations; ++i) {
    run();
  }
}

namespace {

/// Tells the optimiser the value is read, so the work that produced it
/// cannot be removed. The same device as `benchmark::DoNotOptimize`.
template<class T>
void escape(const T& value) {
  asm volatile("" : : "g"(&value) : "memory");
}

using nlohmann_codec = ce::codec::nlohmann_codec;
using rapidjson_codec = ce::bench::rapidjson_codec;
using boost_codec = ce::bench::boost_json_codec;
using glaze_codec = ce::bench::glaze_codec;

// --- the operations --------------------------------------------------------
//
// Each returns whether the SDK call succeeded, so a codec that starts failing
// is reported instead of having its error path measured as if it were the
// work.

template<class C>
auto decode_minimal() -> bool {
  auto event = ce::json_format<C>::decode(ce::bench::minimal_document);
  escape(event);
  return event.has_value();
}

template<class C>
auto decode_full() -> bool {
  auto event = ce::json_format<C>::decode(ce::bench::full_document);
  escape(event);
  return event.has_value();
}

template<class C>
auto decode_large() -> bool {
  auto event = ce::json_format<C>::decode(ce::bench::large_document());
  escape(event);
  return event.has_value();
}

template<class C>
auto encode_full() -> bool {
  auto text = ce::json_format<C>::encode(ce::bench::full_event());
  escape(text);
  return text.has_value();
}

template<class C>
auto roundtrip_full() -> bool {
  auto text = ce::json_format<C>::encode(ce::bench::full_event());
  if (!text) {
    return false;
  }
  auto event = ce::json_format<C>::decode(*text);
  escape(event);
  return event.has_value();
}

template<class C>
auto decode_batch_100() -> bool {
  auto events = ce::json_format<C>::decode_batch(ce::bench::batch_document());
  escape(events);
  return events.has_value() && events->size() == 100;
}

template<class C>
auto encode_batch_100() -> bool {
  auto text =
      ce::json_format<C>::encode_batch(std::span<const ce::event>{ce::bench::batch_events()});
  escape(text);
  return text.has_value();
}

struct order {
  std::int32_t total;
  std::string currency;
  bool paid;
  std::vector<std::string> tags;
};

// Inside the anonymous namespace: CE_DESCRIBE defines a function found by ADL.
CE_DESCRIBE(order, total, currency, paid, tags);

[[nodiscard]] auto typed_event() -> const ce::event& {
  static const ce::event subject = [] {
    using namespace ce::literals;
    ce::event out{"A234"_id, "/orders"_source, "com.example.order"_type};
    ce::set_data<order, nlohmann_codec>(
        out, order{.total = 4299, .currency = "EUR", .paid = true, .tags = {"eu", "priority"}});
    return out;
  }();
  return subject;
}

[[nodiscard]] auto typed_order() -> const order& {
  static const order payload{
      .total = 4299, .currency = "EUR", .paid = true, .tags = {"eu", "priority"}};
  return payload;
}

template<class C>
auto typed_payload_read() -> bool {
  auto payload = ce::data_as<order, C>(typed_event());
  escape(payload);
  return payload.has_value();
}

template<class C>
auto typed_payload_write() -> bool {
  using namespace ce::literals;
  ce::event subject{"A234"_id, "/orders"_source, "com.example.order"_type};
  ce::set_data<order, C>(subject, typed_order());
  escape(subject);
  return true;
}

// --- retained memory ---------------------------------------------------------

/// Live heap bytes while a decoded event is held, minus live bytes before the
/// decode. Scratch the decode frees again is not counted; what the event
/// keeps is.
template<class C>
auto retained_by(std::string_view document) -> std::int64_t {
  const auto before = ce::perf::counters().live_bytes;
  auto event = ce::json_format<C>::decode(document);
  const auto held = ce::perf::counters().live_bytes;
  escape(event);
  return event.has_value() ? held - before : -1;
}

template<class C>
auto retained_minimal() -> std::int64_t {
  return retained_by<C>(ce::bench::minimal_document);
}
template<class C>
auto retained_full() -> std::int64_t {
  return retained_by<C>(ce::bench::full_document);
}
template<class C>
auto retained_large() -> std::int64_t {
  return retained_by<C>(ce::bench::large_document());
}

// --- the table ---------------------------------------------------------------

struct operation {
  std::string id;
  bool (*run)();
  /// Only the decodes produce an event to hold; the rest have none.
  std::int64_t (*retained)() = nullptr;
};

template<class C>
auto operations_for(std::string_view codec) -> std::array<operation, 9> {
  const auto id = [codec](std::string_view name) {
    return std::string{name} + "/" + std::string{codec};
  };
  return {{
      {.id = id("decode_minimal"), .run = decode_minimal<C>, .retained = retained_minimal<C>},
      {.id = id("decode_full"), .run = decode_full<C>, .retained = retained_full<C>},
      {.id = id("decode_large"), .run = decode_large<C>, .retained = retained_large<C>},
      {.id = id("encode_full"), .run = encode_full<C>},
      {.id = id("roundtrip_full"), .run = roundtrip_full<C>},
      {.id = id("decode_batch_100"), .run = decode_batch_100<C>},
      {.id = id("encode_batch_100"), .run = encode_batch_100<C>},
      {.id = id("typed_payload_read"), .run = typed_payload_read<C>},
      {.id = id("typed_payload_write"), .run = typed_payload_write<C>},
  }};
}

[[nodiscard]] auto all_operations() -> const std::array<std::array<operation, 9>, 4>& {
  static const std::array<std::array<operation, 9>, 4> table{{
      operations_for<nlohmann_codec>("nlohmann"),
      operations_for<rapidjson_codec>("rapidjson"),
      operations_for<boost_codec>("boost.json"),
      operations_for<glaze_codec>("glaze"),
  }};
  return table;
}

[[nodiscard]] auto find(std::string_view id) -> const operation* {
  for (const auto& codec : all_operations()) {
    for (const auto& candidate : codec) {
      if (candidate.id == id) {
        return &candidate;
      }
    }
  }
  return nullptr;
}

// --- the modes -----------------------------------------------------------------

/// Runs the operation until static initialisation, lazily built documents and
/// first-use caches are behind it, so a measurement sees the steady state.
[[nodiscard]] auto warm_up(const operation& subject) -> bool {
  constexpr int rounds = 3;
  for (int i = 0; i < rounds; ++i) {
    if (!subject.run()) {
      std::fprintf(stderr, "perf_probe: %s failed; nothing to measure\n", subject.id.c_str());
      return false;
    }
  }
  return true;
}

auto list() -> int {
  std::printf("{\"operations\":[");
  const char* separator = "";
  for (const auto& codec : all_operations()) {
    for (const auto& subject : codec) {
      std::printf("%s\"%s\"", separator, subject.id.c_str());
      separator = ",";
    }
  }
  std::printf("],\"retained\":[");
  separator = "";
  for (const auto& codec : all_operations()) {
    for (const auto& subject : codec) {
      if (subject.retained != nullptr) {
        std::printf("%s\"%s\"", separator, subject.id.c_str());
        separator = ",";
      }
    }
  }
  std::printf("],\"counts_malloc\":%s}\n", ce::perf::counts_malloc() ? "true" : "false");
  return 0;
}

struct allocation_sample {
  std::uint64_t allocations;
  std::uint64_t bytes;
  auto operator==(const allocation_sample&) const -> bool = default;
};

/// Three samples of one operation each. They must agree: an allocation count
/// that moves between identical runs cannot gate on "any increase".
auto alloc(const operation& subject) -> int {
  if (!warm_up(subject)) {
    return 1;
  }
  constexpr std::size_t samples = 3;
  std::array<allocation_sample, samples> taken{};
  for (auto& sample : taken) {
    const auto before = ce::perf::counters();
    subject.run();
    const auto after = ce::perf::counters();
    sample = {.allocations = after.allocations - before.allocations,
              .bytes = after.bytes - before.bytes};
  }
  for (const auto& sample : taken) {
    if (sample != taken[0]) {
      std::fprintf(stderr,
                   "perf_probe: %s allocated differently across identical runs "
                   "(%llu/%llu, %llu/%llu, %llu/%llu allocations/bytes); "
                   "the allocation gate needs a deterministic count\n",
                   subject.id.c_str(),
                   static_cast<unsigned long long>(taken[0].allocations),
                   static_cast<unsigned long long>(taken[0].bytes),
                   static_cast<unsigned long long>(taken[1].allocations),
                   static_cast<unsigned long long>(taken[1].bytes),
                   static_cast<unsigned long long>(taken[2].allocations),
                   static_cast<unsigned long long>(taken[2].bytes));
      return 1;
    }
  }
  std::printf(
      "{\"id\":\"%s\",\"allocations\":%llu,\"allocated_bytes\":%llu,\"counts_malloc\":%s}\n",
      subject.id.c_str(),
      static_cast<unsigned long long>(taken[0].allocations),
      static_cast<unsigned long long>(taken[0].bytes),
      ce::perf::counts_malloc() ? "true" : "false");
  return 0;
}

auto retained(const operation& subject) -> int {
  if (subject.retained == nullptr) {
    std::fprintf(stderr, "perf_probe: %s produces no event to hold\n", subject.id.c_str());
    return 2;
  }
  if (!warm_up(subject)) {
    return 1;
  }
  const std::array<std::int64_t, 3> taken{
      subject.retained(), subject.retained(), subject.retained()};
  if (taken[0] < 0 || taken[1] != taken[0] || taken[2] != taken[0]) {
    std::fprintf(stderr,
                 "perf_probe: %s retained %lld, %lld and %lld bytes across identical runs\n",
                 subject.id.c_str(),
                 static_cast<long long>(taken[0]),
                 static_cast<long long>(taken[1]),
                 static_cast<long long>(taken[2]));
    return 1;
  }
  std::printf("{\"id\":\"%s\",\"retained_bytes\":%lld,\"counts_malloc\":%s}\n",
              subject.id.c_str(),
              static_cast<long long>(taken[0]),
              ce::perf::counts_malloc() ? "true" : "false");
  return 0;
}

auto instr(const operation& subject, std::string_view count) -> int {
  std::uint64_t iterations = 0;
  const auto parsed = std::from_chars(count.data(), count.data() + count.size(), iterations);
  if (parsed.ec != std::errc{} || parsed.ptr != count.data() + count.size() || iterations == 0) {
    std::fprintf(stderr, "perf_probe: iterations must be a positive integer\n");
    return 2;
  }
  if (!warm_up(subject)) {
    return 1;
  }
  ce_perf_measured_loop(subject.run, iterations);
  std::printf("{\"id\":\"%s\",\"iterations\":%llu}\n",
              subject.id.c_str(),
              static_cast<unsigned long long>(iterations));
  return 0;
}

auto usage() -> int {
  std::fprintf(stderr,
               "usage: perf_probe list\n"
               "       perf_probe alloc <id>\n"
               "       perf_probe retained <id>\n"
               "       perf_probe instr <id> <iterations>\n");
  return 2;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
  if (args.size() < 2) {
    return usage();
  }
  const std::string_view mode{args[1]};
  if (mode == "list") {
    return list();
  }
  if (args.size() < 3) {
    return usage();
  }
  const operation* subject = find(args[2]);
  if (subject == nullptr) {
    std::fprintf(stderr, "perf_probe: no operation '%s'; see perf_probe list\n", args[2]);
    return 2;
  }
  if (mode == "alloc") {
    return alloc(*subject);
  }
  if (mode == "retained") {
    return retained(*subject);
  }
  if (mode == "instr" && args.size() == 4) {
    return instr(*subject, args[3]);
  }
  return usage();
}
