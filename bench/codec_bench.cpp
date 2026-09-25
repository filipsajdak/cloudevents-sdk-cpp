/// \file
/// \brief What the JSON library choice costs on CloudEvents workloads.
///
/// The SDK takes its codec as a template parameter, so the library is the
/// caller's choice. This measures that choice on the operations a service
/// actually performs - decoding a received event, encoding one to send, and the
/// batch forms - rather than on generic JSON, where a parser's throughput on a
/// megabyte of nested arrays says little about a 400-byte event.
///
/// Every codec here has already passed codec_check, which is the only reason
/// the numbers are comparable.

#include <benchmark/benchmark.h>
#include <cstdint>
#include <simdjson.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <yyjson.h>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>

#include "codecs/boost_json_codec.hpp"
#include "codecs/glaze_codec.hpp"
#include "codecs/rapidjson_codec.hpp"
#include "documents.hpp"
#include "typed_documents.hpp"

namespace {

using nlohmann_codec = ce::codec::nlohmann_codec;
using rapidjson_codec = ce::bench::rapidjson_codec;
using boost_codec = ce::bench::boost_json_codec;
using glaze_codec = ce::bench::glaze_codec;

// --- the SDK's own operations ----------------------------------------------

template <class C>
void decode_minimal(benchmark::State& state) {
  for (auto _ : state) {
    auto event = ce::json_format<C>::decode(ce::bench::minimal_document);
    benchmark::DoNotOptimize(event);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(ce::bench::minimal_document.size()));
}

template <class C>
void decode_full(benchmark::State& state) {
  for (auto _ : state) {
    auto event = ce::json_format<C>::decode(ce::bench::full_document);
    benchmark::DoNotOptimize(event);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(ce::bench::full_document.size()));
}

template <class C>
void decode_large(benchmark::State& state) {
  const auto& document = ce::bench::large_document();
  for (auto _ : state) {
    auto event = ce::json_format<C>::decode(document);
    benchmark::DoNotOptimize(event);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(document.size()));
}

template <class C>
void encode_full(benchmark::State& state) {
  const ce::event& subject = ce::bench::full_event();
  for (auto _ : state) {
    auto text = ce::json_format<C>::encode(subject);
    benchmark::DoNotOptimize(text);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(ce::bench::full_document.size()));
}

template <class C>
void roundtrip_full(benchmark::State& state) {
  const ce::event& subject = ce::bench::full_event();
  for (auto _ : state) {
    auto text = ce::json_format<C>::encode(subject);
    auto event = ce::json_format<C>::decode(*text);
    benchmark::DoNotOptimize(event);
  }
}

template <class C>
void decode_batch_100(benchmark::State& state) {
  const auto& document = ce::bench::batch_document();
  for (auto _ : state) {
    auto events = ce::json_format<C>::decode_batch(document);
    benchmark::DoNotOptimize(events);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(document.size()));
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 100);
}

template <class C>
void encode_batch_100(benchmark::State& state) {
  const auto& events = ce::bench::batch_events();
  for (auto _ : state) {
    auto text = ce::json_format<C>::encode_batch(std::span<const ce::event>{events});
    benchmark::DoNotOptimize(text);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * 100);
}

// --- the typed payload path, which goes through describe_json ---------------

template<class C>
void typed_payload_read(benchmark::State& state) {
  const ce::event& subject = ce::bench::typed_event<C>();
  for (auto _ : state) {
    auto payload = ce::data_as<ce::bench::order, C>(subject);
    benchmark::DoNotOptimize(payload);
  }
}

template<class C>
void typed_payload_read_document(benchmark::State& state) {
  const ce::event& subject = ce::bench::typed_document_event<C>();
  for (auto _ : state) {
    auto payload = ce::data_as<ce::bench::order, C>(subject);
    benchmark::DoNotOptimize(payload);
  }
}

template <class C>
void typed_payload_write(benchmark::State& state) {
  using namespace ce::literals;
  for (auto _ : state) {
    ce::event subject{"A234"_id, "/orders"_source, "com.example.order"_type};
    ce::set_data<ce::bench::order, C>(subject, ce::bench::typed_order());
    benchmark::DoNotOptimize(subject);
  }
}

// --- the typed entry points --------------------------------------------------

template<class C>
void decode_as_full(benchmark::State& state) {
  for (auto _ : state) {
    auto decoded = ce::decode_as<ce::bench::placed, C>(ce::bench::full_document);
    benchmark::DoNotOptimize(decoded);
  }
}

template<class C>
void decode_as_large(benchmark::State& state) {
  for (auto _ : state) {
    auto decoded = ce::decode_as<ce::bench::report, C>(ce::bench::large_document());
    benchmark::DoNotOptimize(decoded);
  }
}

template<class C>
void decode_batch_as_100(benchmark::State& state) {
  for (auto _ : state) {
    auto decoded = ce::decode_batch_as<ce::bench::tick, C>(ce::bench::batch_document());
    benchmark::DoNotOptimize(decoded);
  }
}

template<class C>
void encode_as_full(benchmark::State& state) {
  for (auto _ : state) {
    auto text =
        ce::encode_as<ce::bench::placed, C>(ce::bench::full_event(), ce::bench::typed_placed());
    benchmark::DoNotOptimize(text);
  }
}

// --- what a codec cannot reach ---------------------------------------------
//
// simdjson and yyjson cannot satisfy ce::json::json_codec, for a reason the
// concept makes explicit: find() returns `const value*`, a pointer INTO the
// document. simdjson's on-demand values are a forward-only stream and its DOM
// elements are returned by value; yyjson hands out node handles, not addresses
// of stored values. Neither has anything to take the address of.
//
// They are measured anyway, parsing the same bytes, because the honest question
// is not "which codec is fastest" but "what is the concept costing". A number
// no codec can reach is the only way to answer it.

void simdjson_parse_full(benchmark::State& state) {
  simdjson::ondemand::parser parser;
  const auto padded = simdjson::padded_string{std::string{ce::bench::full_document}};
  for (auto _ : state) {
    auto document = parser.iterate(padded);
    std::string_view id;
    auto error = document["id"].get_string().get(id);
    benchmark::DoNotOptimize(error);
    benchmark::DoNotOptimize(id);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(ce::bench::full_document.size()));
}

void simdjson_parse_large(benchmark::State& state) {
  simdjson::ondemand::parser parser;
  const auto padded = simdjson::padded_string{ce::bench::large_document()};
  for (auto _ : state) {
    auto document = parser.iterate(padded);
    std::string_view id;
    auto error = document["id"].get_string().get(id);
    benchmark::DoNotOptimize(error);
    benchmark::DoNotOptimize(id);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(ce::bench::large_document().size()));
}

void yyjson_parse_full(benchmark::State& state) {
  for (auto _ : state) {
    yyjson_doc* document = yyjson_read(ce::bench::full_document.data(),
                                       ce::bench::full_document.size(), 0);
    yyjson_val* id = yyjson_obj_get(yyjson_doc_get_root(document), "id");
    benchmark::DoNotOptimize(id);
    yyjson_doc_free(document);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(ce::bench::full_document.size()));
}

void yyjson_parse_large(benchmark::State& state) {
  const auto& text = ce::bench::large_document();
  for (auto _ : state) {
    yyjson_doc* document = yyjson_read(text.data(), text.size(), 0);
    yyjson_val* id = yyjson_obj_get(yyjson_doc_get_root(document), "id");
    benchmark::DoNotOptimize(id);
    yyjson_doc_free(document);
  }
  state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(text.size()));
}

}  // namespace

#define CE_BENCH_ALL(op)                             \
  BENCHMARK(op<nlohmann_codec>)->Name(#op "/nlohmann");   \
  BENCHMARK(op<rapidjson_codec>)->Name(#op "/rapidjson"); \
  BENCHMARK(op<boost_codec>)->Name(#op "/boost.json");    \
  BENCHMARK(op<glaze_codec>)->Name(#op "/glaze")

CE_BENCH_ALL(decode_minimal);
CE_BENCH_ALL(decode_full);
CE_BENCH_ALL(decode_large);
CE_BENCH_ALL(encode_full);
CE_BENCH_ALL(roundtrip_full);
CE_BENCH_ALL(decode_batch_100);
CE_BENCH_ALL(encode_batch_100);
CE_BENCH_ALL(typed_payload_read);
CE_BENCH_ALL(typed_payload_read_document);
CE_BENCH_ALL(typed_payload_write);
CE_BENCH_ALL(decode_as_full);
CE_BENCH_ALL(decode_as_large);
CE_BENCH_ALL(decode_batch_as_100);
CE_BENCH_ALL(encode_as_full);

BENCHMARK(simdjson_parse_full)->Name("ceiling_parse_full/simdjson");
BENCHMARK(yyjson_parse_full)->Name("ceiling_parse_full/yyjson");
BENCHMARK(simdjson_parse_large)->Name("ceiling_parse_large/simdjson");
BENCHMARK(yyjson_parse_large)->Name("ceiling_parse_large/yyjson");

BENCHMARK_MAIN();
