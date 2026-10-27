#pragma once

/// \file
/// \brief The events of the Protobuf interoperability corpus, from the C++ side.
///
/// The Go generator (interop/protobuf/go) and the Java generator (interop/protobuf/java) write
/// the same thirteen events under the same names. This header is shared by the producer that
/// writes the C++ bytes and by the test that reads every producer's bytes back (SWR-PROTO-0016).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cloudevents/core.hpp>
#include <cloudevents/detail/timestamp.hpp>

namespace ce::interop::protobuf_corpus {

using namespace ce::literals;

struct named_event {
  std::string name;
  ce::event event;
};

[[nodiscard]] inline auto instant(std::string_view text) -> ce::timestamp {
  const auto parsed = ce::parse_timestamp(text);
  return parsed ? *parsed : ce::timestamp{};
}

/// A `google.protobuf.Struct` of {"count": 3, "label": "interop", "ok": true}, the entries in
/// key order as protobuf-go writes them deterministically: the payload of the proto_data events.
[[nodiscard]] inline auto struct_payload() -> ce::binary {
  constexpr std::array<std::uint8_t, 50> octets{
      0x0A, 0x12, 0x0A, 0x05, 'c',  'o',  'u',  'n',  't',  0x12, 0x09, 0x11, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x40, 0x0A, 0x12, 0x0A, 0x05, 'l',  'a',
      'b',  'e',  'l',  0x12, 0x09, 0x1A, 0x07, 'i',  'n',  't',  'e',  'r',  'o',
      'p',  0x0A, 0x08, 0x0A, 0x02, 'o',  'k',  0x12, 0x02, 0x20, 0x01};
  ce::binary out;
  for (const auto octet : octets) {
    out.push_back(static_cast<std::byte>(octet));
  }
  return out;
}

[[nodiscard]] inline auto json(std::string_view body) -> ce::json_text {
  return ce::json_text{.raw = std::string{body}};
}

/// The same events the Go and Java generators write, from this side.
[[nodiscard]] inline auto events() -> std::vector<named_event> {
  std::vector<named_event> out;
  out.push_back(
      {"minimal",
       ce::event{"id-minimal"_id, "/interop/protobuf"_source, "com.example.minimal"_type}});
  out.push_back({"full",
                 ce::event{"id-full"_id,
                           "https://example.test/interop/protobuf"_source,
                           "com.example.full"_type,
                           {
                               .datacontenttype = "application/json"_mediatype,
                               .dataschema = "https://example.test/schema/1"_dataschema,
                               .subject = "the-subject"_subject,
                               .time = instant("2026-09-20T12:34:56Z"),
                               .data = json(R"({"count":3,"label":"interop"})"),
                           }}});
  out.push_back(
      {"extensions",
       ce::event{"id-extensions"_id,
                 "/interop/protobuf"_source,
                 "com.example.extensions"_type,
                 {.extensions = {
                      {"traceparent"_ext,
                       std::string{"00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}},
                      {"sampledrate"_ext, std::int32_t{30}},
                      {"partitionkey"_ext, std::string{"customer-42"}},
                  }}}});
  out.push_back({"text_data",
                 ce::event{"id-text"_id,
                           "/interop/protobuf"_source,
                           "com.example.text"_type,
                           {.datacontenttype = "text/plain"_mediatype,
                            .data = std::string{"plain text payload"}}}});
  out.push_back(
      {"binary_data",
       ce::event{"id-binary"_id,
                 "/interop/protobuf"_source,
                 "com.example.binary"_type,
                 {.datacontenttype = "application/octet-stream"_mediatype,
                  .data = ce::binary{
                      std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}}}}});
  out.push_back({"time_nanoseconds",
                 ce::event{"id-nanos"_id,
                           "/interop/protobuf"_source,
                           "com.example.nanos"_type,
                           {.time = instant("2026-09-20T12:34:56.123456789+02:00")}}});
  out.push_back({"extension_types",
                 ce::event{"id-types"_id,
                           "/interop/protobuf"_source,
                           "com.example.types"_type,
                           {.extensions = {
                                {"astring"_ext, std::string{"text"}},
                                {"aninteger"_ext, std::int32_t{42}},
                                {"aboolean"_ext, true},
                                {"negative"_ext, std::int32_t{-7}},
                                {"zero"_ext, std::int32_t{0}},
                                {"maxint"_ext, std::int32_t{2147483647}},
                                {"minint"_ext, std::int32_t{-2147483647 - 1}},
                                {"abinary"_ext,
                                 ce::binary{std::byte{0x00}, std::byte{0xFF}, std::byte{0x10}}},
                                {"auri"_ext, ce::uri{"https://example.test/ext?q=1"}},
                                {"atimestamp"_ext, instant("2026-09-20T12:34:56Z")},
                            }}}});
  out.push_back({"unicode",
                 ce::event{"id-unicode-é文"_id,
                           "/interop/protobuf/événement"_source,
                           "com.example.unicode"_type,
                           {.datacontenttype = "application/json"_mediatype,
                            .subject = "日本語 \U0001F600"_subject,
                            .data = json("{\"text\":\"é文 \U0001F600\"}")}}});
  out.push_back({"data_array",
                 ce::event{"id-array"_id,
                           "/interop/protobuf"_source,
                           "com.example.array"_type,
                           {.datacontenttype = "application/json"_mediatype,
                            .data = json(R"([1,"two",3.5])")}}});
  out.push_back(
      {"minimal_relative",
       ce::event{"id-relative"_id, "../relative/ref"_source, "com.example.relative"_type}});
  out.push_back({"proto_data",
                 ce::event{"id-proto"_id,
                           "/interop/protobuf"_source,
                           "com.example.proto"_type,
                           {.datacontenttype = "application/protobuf"_mediatype,
                            .dataschema = "type.googleapis.com/google.protobuf.Struct"_dataschema,
                            .data = struct_payload()}}});
  out.push_back(
      {"proto_data_noschema",
       ce::event{"id-proto-noschema"_id,
                 "/interop/protobuf"_source,
                 "com.example.proto"_type,
                 {.datacontenttype = "application/protobuf"_mediatype, .data = struct_payload()}}});
  out.push_back({"text_data_empty",
                 ce::event{"id-empty-text"_id,
                           "/interop/protobuf"_source,
                           "com.example.text"_type,
                           {.datacontenttype = "text/plain"_mediatype, .data = std::string{}}}});
  return out;
}

}  // namespace ce::interop::protobuf_corpus
