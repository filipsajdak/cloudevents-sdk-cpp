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

  ce::event minimal{
      .id = "id-minimal", .source = ce::uri_ref{"/interop/cpp"}, .type = "com.example.minimal"};
  write(dir, "minimal", minimal);

  ce::event full{
      .id = "id-full", .source = ce::uri_ref{"https://example.test/interop/cpp"},
      .type = "com.example.full"};
  full.subject = "the-subject";
  full.dataschema = ce::uri{"https://example.test/schema/1"};
  full.datacontenttype = "application/json";
  if (auto parsed = ce::parse_timestamp("2026-09-20T12:34:56Z")) {
    full.time = *parsed;
  }
  full.data = ce::json_text{.raw = R"({"count":3,"label":"cpp"})"};
  write(dir, "full", full);

  ce::event extended{.id = "id-extensions", .source = ce::uri_ref{"/interop/cpp"},
                     .type = "com.example.extensions"};
  (void)extended.set(ce::ext::tracing{
      .traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01", .tracestate = {}});
  (void)extended.set(ce::ext::sampled_rate{.sampledrate = 30});
  (void)extended.set(ce::ext::partitioning{.partitionkey = "customer-42"});
  write(dir, "extensions", extended);

  ce::event text{
      .id = "id-text", .source = ce::uri_ref{"/interop/cpp"}, .type = "com.example.text"};
  text.datacontenttype = "text/plain";
  text.data = std::string{"plain text payload"};
  write(dir, "text_data", text);

  ce::event binary{
      .id = "id-binary", .source = ce::uri_ref{"/interop/cpp"}, .type = "com.example.binary"};
  binary.datacontenttype = "application/octet-stream";
  binary.data = ce::binary{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}};
  write(dir, "binary_data", binary);

  // --- the same edge cases, from this side --------------------------------

  ce::event nanos{
      .id = "id-nanos", .source = ce::uri_ref{"/interop/cpp"}, .type = "com.example.nanos"};
  if (auto parsed = ce::parse_timestamp("2026-09-20T12:34:56.123456789+02:00")) {
    nanos.time = *parsed;
  } else {
    std::fprintf(stderr, "FAIL: this SDK cannot parse nanosecond precision\n");
    ++failures;
  }
  write(dir, "time_nanoseconds", nanos);

  ce::event types{
      .id = "id-types", .source = ce::uri_ref{"/interop/cpp"}, .type = "com.example.types"};
  (void)types.set_extension("astring", ce::attribute_value{std::string{"text"}});
  (void)types.set_extension("aninteger", ce::attribute_value{std::int32_t{42}});
  (void)types.set_extension("aboolean", ce::attribute_value{true});
  (void)types.set_extension("negative", ce::attribute_value{std::int32_t{-7}});
  (void)types.set_extension("zero", ce::attribute_value{std::int32_t{0}});
  write(dir, "extension_types", types);

  ce::event unicode{.id = "id-unicode-\u00e9\u6587",
                    .source = ce::uri_ref{"/interop/cpp/\u00e9v\u00e9nement"},
                    .type = "com.example.unicode"};
  unicode.subject = "\u65e5\u672c\u8a9e \U0001F600";
  unicode.datacontenttype = "application/json";
  // Real UTF-8 bytes rather than escapes: JSON has no \U form, only \uXXXX
  // and surrogate pairs, and the point is to carry the characters themselves.
  unicode.data = ce::json_text{.raw = "{\"text\":\"\u00e9\u6587 \U0001F600\"}"};
  write(dir, "unicode", unicode);

  ce::event scalar{
      .id = "id-scalar", .source = ce::uri_ref{"/interop/cpp"}, .type = "com.example.scalar"};
  scalar.datacontenttype = "application/json";
  scalar.data = ce::json_text{.raw = "[1,2,3]"};
  write(dir, "data_array", scalar);

  ce::event relative{
      .id = "id-relative", .source = ce::uri_ref{"/"}, .type = "t"};
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
