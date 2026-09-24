/// \file
/// \brief The smallest program that decodes and encodes an event with one
/// codec. Its stripped size is the binary-size measure (SWR-PERF-0006).
///
/// Built once per codec; CMake names the codec's header and type in
/// CE_PERF_CODEC_HEADER and CE_PERF_CODEC_TYPE, so the one source serves all
/// four without a preprocessor branch.

#include <cloudevents/core.hpp>
#include <cloudevents/format/json_format.hpp>

#include CE_PERF_CODEC_HEADER

#include <cstdio>
#include <span>
#include <string_view>

auto main(int argc, char** argv) -> int {
  using format = ce::json_format<CE_PERF_CODEC_TYPE>;
  const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
  const std::string_view document =
      args.size() > 1 ? std::string_view{args[1]}
                      : R"({"specversion":"1.0","id":"1","source":"/s","type":"t","n":7})";
  auto decoded = format::decode(document);
  if (!decoded) {
    return 1;
  }
  auto encoded = format::encode(*decoded);
  if (!encoded) {
    return 2;
  }
  std::printf("%zu\n", encoded->size());
  return 0;
}
