/// \file
/// \brief Emits the Kafka records the Go and Java SDKs are asked to accept.
///
/// The records are the key, the headers and the value this SDK's Kafka binding
/// puts on a record, in the JSON shape the Go and Java generators write, so that
/// no broker is involved (SWR-SEC-0008). Built by interop/run.sh, not by the
/// default CMake build.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <cloudevents/binding/kafka.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/base64.hpp>

namespace {

using codec = ce::codec::nlohmann_codec;
using namespace ce::literals;

/// JSON object keys keep the order they are inserted in, as in the other producers.
using ordered_json = nlohmann::ordered_json;

constexpr int json_indent = 2;

constexpr std::array modes{
    std::pair{std::string_view{"binary"}, ce::content_mode::binary_mode},
    std::pair{std::string_view{"structured"}, ce::content_mode::structured},
};

int failures = 0;

[[nodiscard]] auto base64_of(std::string_view text) -> std::string {
  ce::binary bytes;
  bytes.reserve(text.size());
  for (const char character : text) {
    bytes.push_back(static_cast<std::byte>(character));
  }
  return ce::base64_encode(bytes);
}

[[nodiscard]] auto instant(std::string_view text) -> std::optional<ce::timestamp> {
  auto parsed = ce::parse_timestamp(text);
  if (!parsed) {
    std::fprintf(
        stderr, "FAIL: this SDK cannot parse %.*s\n", static_cast<int>(text.size()), text.data());
    ++failures;
    return std::nullopt;
  }
  return *parsed;
}

/// A record as the producers write it. The value of an event with no payload is
/// the empty string here, a record this SDK sends with an empty value rather than
/// a null one.
[[nodiscard]] auto document_of(const ce::kafka::record& produced) -> ordered_json {
  auto headers = ordered_json::array();
  for (const auto& [name, value] : produced.value.header_fields) {
    headers.push_back(ordered_json::array({name, base64_of(value)}));
  }
  // Parentheses: a braced one-element list would build an array, not a string.
  const ordered_json key =
      produced.key ? ordered_json(base64_of(*produced.key)) : ordered_json(nullptr);
  return ordered_json{
      {"key", key},
      {"headers", std::move(headers)},
      {"value", ce::base64_encode(produced.value.body)},
  };
}

void write(const std::filesystem::path& dir, std::string_view name, const ce::event& subject) {
  for (const auto& [mode_name, mode] : modes) {
    auto produced = ce::kafka::to_record<codec, ce::kafka::partitionkey_mapper>(subject, mode);
    if (!produced) {
      std::fprintf(stderr,
                   "FAIL %.*s.%.*s: %s\n",
                   static_cast<int>(name.size()),
                   name.data(),
                   static_cast<int>(mode_name.size()),
                   mode_name.data(),
                   produced.error().detail.c_str());
      ++failures;
      continue;
    }
    const std::string file = std::string{name} + "." + std::string{mode_name} + ".json";
    std::ofstream out{dir / file, std::ios::binary};
    out << document_of(*produced).dump(json_indent) << '\n';
    std::printf("wrote %s\n", file.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: produce <output-directory>\n");
    return 2;
  }
  const std::filesystem::path dir{argv[1]};
  std::filesystem::create_directories(dir);

  // The same events the Go and Java generators write, from this side.
  write(dir,
        "minimal",
        ce::event{"id-minimal"_id, "/interop/cpp"_source, "com.example.minimal"_type});

  write(dir,
        "full",
        ce::event{"id-full"_id,
                  "https://example.test/interop/cpp"_source,
                  "com.example.full"_type,
                  {
                      .datacontenttype = "application/json"_mediatype,
                      .dataschema = "https://example.test/schema/1"_dataschema,
                      .subject = "the-subject"_subject,
                      .time = instant("2026-09-20T12:34:56Z"),
                      .data = ce::json_text{.raw = R"({"count":3,"label":"cpp"})"},
                  }});

  write(dir,
        "extensions",
        ce::event{"id-extensions"_id,
                  "/interop/cpp"_source,
                  "com.example.extensions"_type,
                  {.extensions = {
                       {"traceparent"_ext,
                        std::string{"00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}},
                       {"sampledrate"_ext, std::int32_t{30}},
                       {"partitionkey"_ext, std::string{"customer-42"}},
                   }}});

  write(dir,
        "text_data",
        ce::event{"id-text"_id,
                  "/interop/cpp"_source,
                  "com.example.text"_type,
                  {.datacontenttype = "text/plain"_mediatype,
                   .data = std::string{"plain text payload"}}});

  write(dir,
        "binary_data",
        ce::event{
            "id-binary"_id,
            "/interop/cpp"_source,
            "com.example.binary"_type,
            {
                .datacontenttype = "application/octet-stream"_mediatype,
                .data =
                    ce::binary{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}},
            }});

  write(dir,
        "time_nanoseconds",
        ce::event{"id-nanos"_id,
                  "/interop/cpp"_source,
                  "com.example.nanos"_type,
                  {.time = instant("2026-09-20T12:34:56.123456789+02:00")}});

  write(dir,
        "extension_types",
        ce::event{"id-types"_id,
                  "/interop/cpp"_source,
                  "com.example.types"_type,
                  {.extensions = {
                       {"astring"_ext, std::string{"text"}},
                       {"aninteger"_ext, std::int32_t{42}},
                       {"aboolean"_ext, true},
                       {"negative"_ext, std::int32_t{-7}},
                       {"zero"_ext, std::int32_t{0}},
                   }}});

  // Real UTF-8 bytes rather than escapes, in the headers as well as the payload.
  write(dir,
        "unicode",
        ce::event{"id-unicode-é文"_id,
                  "/interop/cpp/événement"_source,
                  "com.example.unicode"_type,
                  {
                      .datacontenttype = "application/json"_mediatype,
                      .subject = "日本語 \U0001F600"_subject,
                      .data = ce::json_text{.raw = "{\"text\":\"é文 \U0001F600\"}"},
                  }});

  // A percent in a header value: Kafka has no escaping rule, so it travels as is.
  write(dir,
        "percent_in_subject",
        ce::event{"id-percent"_id,
                  "/interop/cpp"_source,
                  "com.example.percent"_type,
                  {.subject = "100% of 50%OFF"_subject,
                   .extensions = {{"pct"_ext, std::string{"a%20b"}}}}});

  write(dir,
        "data_array",
        ce::event{"id-scalar"_id,
                  "/interop/cpp"_source,
                  "com.example.scalar"_type,
                  {.datacontenttype = "application/json"_mediatype,
                   .data = ce::json_text{.raw = "[1,2,3]"}}});

  write(dir, "minimal_relative", ce::event{"id-relative"_id, "/"_source, "t"_type});

  return failures == 0 ? 0 : 1;
}
