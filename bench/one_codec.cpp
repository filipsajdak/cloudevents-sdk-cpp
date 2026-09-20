/// \file
/// \brief One codec, one translation unit, for measuring what it costs to
/// compile and what it adds to a binary.
///
/// Built once per codec with -DCE_BENCH_CODEC=<name>. The body is the smallest
/// thing that instantiates the whole format layer, because that is what a
/// consumer's translation unit does.

#include <cloudevents/core.hpp>
#include <cloudevents/format/json_format.hpp>

#include <cstdio>
#include <span>
#include <string_view>
#include <vector>

#if CE_BENCH_CODEC == 1
#include <cloudevents/codec/nlohmann.hpp>
using codec = ce::codec::nlohmann_codec;
#elif CE_BENCH_CODEC == 2
#include "codecs/rapidjson_codec.hpp"
using codec = ce::bench::rapidjson_codec;
#elif CE_BENCH_CODEC == 3
#include "codecs/boost_json_codec.hpp"
using codec = ce::bench::boost_json_codec;
#elif CE_BENCH_CODEC == 4
#include "codecs/glaze_codec.hpp"
using codec = ce::bench::glaze_codec;
#else
#error "define CE_BENCH_CODEC to 1..4"
#endif

int main(int argc, char** argv) {
  using format = ce::json_format<codec>;
  static_assert(ce::json::json_codec<codec>);

  const std::string_view document =
      argc > 1 ? std::string_view{argv[1]}
               : R"({"specversion":"1.0","id":"1","source":"/s","type":"t","n":7})";

  auto decoded = format::decode(document);
  if (!decoded) {
    std::printf("decode: %s\n", decoded.error().detail.c_str());
    return 1;
  }
  auto encoded = format::encode(*decoded);
  if (!encoded) {
    return 2;
  }
  auto batch = format::decode_batch("[]");
  if (!batch) {
    return 3;
  }
  const std::vector<ce::event> events{*decoded};
  auto batch_text = format::encode_batch(std::span<const ce::event>{events});
  if (!batch_text) {
    return 4;
  }
  std::printf("%zu %zu\n", encoded->size(), batch_text->size());
  return 0;
}
