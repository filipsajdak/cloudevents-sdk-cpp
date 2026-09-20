/// \file
/// \brief Emits the documents the Go and Java SDKs are asked to accept.
///
/// Committed with its output so a fixture can be regenerated and audited
/// (SWR-SEC-0005). Built by interop/run.sh, not by the default CMake build.

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/json_format.hpp>

#include <cstdio>
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

  return failures == 0 ? 0 : 1;
}
