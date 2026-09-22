/// \file
/// \brief Emits the documents the Go and Java SDKs are asked to accept.
///
/// Committed with its output so a fixture can be regenerated and audited
/// (SWR-SEC-0005). Built by interop/run.sh, not by the default CMake build.

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/json_format.hpp>

#include <cstdint>
#include <cstdio>
#include <span>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using codec = ce::codec::nlohmann_codec;
using format = ce::json_format<codec>;

int failures = 0;

void write(const std::filesystem::path& dir, const std::string& name, const ce::event& subject) {
  auto encoded = format::encode(subject);
  if (!encoded) {
    std::fprintf(stderr, "FAIL %s: %s\n", name.c_str(), encoded.error().detail.c_str());
    ++failures;
    return;
  }
  std::ofstream out{dir / (name + ".json"), std::ios::binary};
  out << *encoded;
  std::printf("wrote %s.json\n", name.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: produce <output-directory>\n");
    return 2;
  }
  const std::filesystem::path dir{argv[1]};
  std::filesystem::create_directories(dir);

  using namespace ce::literals;

  const ce::event minimal{"id-minimal"_id, "/interop/cpp"_source, "com.example.minimal"_type};
  write(dir, "minimal", minimal);

  const auto full_time = ce::parse_timestamp("2026-09-20T12:34:56Z");
  const ce::event full{
      "id-full"_id,
      "https://example.test/interop/cpp"_source,
      "com.example.full"_type,
      {
          .datacontenttype = "application/json"_mediatype,
          .dataschema = "https://example.test/schema/1"_dataschema,
          .subject = "the-subject"_subject,
          .time = full_time ? std::optional<ce::timestamp>{*full_time} : std::nullopt,
          .data = ce::json_text{.raw = R"({"count":3,"label":"cpp"})"},
      },
  };
  write(dir, "full", full);

  // The typed extensions name their attributes from the struct, so writing one
  // can be refused; a refusal is a failed producer run, not a silent omission.
  ce::event extended{"id-extensions"_id, "/interop/cpp"_source, "com.example.extensions"_type};
  const bool extended_written =
      extended.set(ce::ext::tracing{
                       .traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
                       .tracestate = {}})
          .has_value() &&
      extended.set(ce::ext::sampled_rate{.sampledrate = 30}).has_value() &&
      extended.set(ce::ext::partitioning{.partitionkey = "customer-42"}).has_value();
  if (!extended_written) {
    std::fprintf(stderr, "FAIL: a typed extension was refused\n");
    ++failures;
  }
  write(dir, "extensions", extended);

  const ce::event text{"id-text"_id, "/interop/cpp"_source, "com.example.text"_type,
                       {.datacontenttype = "text/plain"_mediatype,
                        .data = std::string{"plain text payload"}}};
  write(dir, "text_data", text);

  const ce::event binary{
      "id-binary"_id,
      "/interop/cpp"_source,
      "com.example.binary"_type,
      {
          .datacontenttype = "application/octet-stream"_mediatype,
          .data = ce::binary{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}},
      },
  };
  write(dir, "binary_data", binary);

  // --- the same edge cases, from this side --------------------------------

  const auto nanos_time = ce::parse_timestamp("2026-09-20T12:34:56.123456789+02:00");
  if (!nanos_time) {
    std::fprintf(stderr, "FAIL: this SDK cannot parse nanosecond precision\n");
    ++failures;
  }
  const ce::event nanos{
      "id-nanos"_id, "/interop/cpp"_source, "com.example.nanos"_type,
      {.time = nanos_time ? std::optional<ce::timestamp>{*nanos_time} : std::nullopt}};
  write(dir, "time_nanoseconds", nanos);

  const ce::event types{"id-types"_id, "/interop/cpp"_source, "com.example.types"_type,
                        {.extensions = {
                             {"astring"_ext, std::string{"text"}},
                             {"aninteger"_ext, std::int32_t{42}},
                             {"aboolean"_ext, true},
                             {"negative"_ext, std::int32_t{-7}},
                             {"zero"_ext, std::int32_t{0}},
                         }}};
  write(dir, "extension_types", types);

  // Real UTF-8 bytes rather than escapes in the payload: JSON has no \U form,
  // only \uXXXX and surrogate pairs, and the point is to carry the characters
  // themselves.
  const ce::event unicode{
      "id-unicode-\u00e9\u6587"_id,
      "/interop/cpp/\u00e9v\u00e9nement"_source,
      "com.example.unicode"_type,
      {
          .datacontenttype = "application/json"_mediatype,
          .subject = "\u65e5\u672c\u8a9e \U0001F600"_subject,
          .data = ce::json_text{.raw = "{\"text\":\"\u00e9\u6587 \U0001F600\"}"},
      },
  };
  write(dir, "unicode", unicode);

  const ce::event scalar{"id-scalar"_id, "/interop/cpp"_source, "com.example.scalar"_type,
                         {.datacontenttype = "application/json"_mediatype,
                          .data = ce::json_text{.raw = "[1,2,3]"}}};
  write(dir, "data_array", scalar);

  const ce::event relative{"id-relative"_id, "/"_source, "t"_type};
  write(dir, "minimal_relative", relative);

  // A batch, which nothing had exercised across SDKs.
  const std::vector<ce::event> events{minimal, full, extended};
  auto batch = format::encode_batch(std::span<const ce::event>{events});
  if (!batch) {
    std::fprintf(stderr, "FAIL batch: %s\n", batch.error().detail.c_str());
    ++failures;
  } else {
    std::ofstream out{dir / "batch.json", std::ios::binary};
    out << *batch;
    std::printf("wrote batch.json\n");
  }

  return failures == 0 ? 0 : 1;
}
