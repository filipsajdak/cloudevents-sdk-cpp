#pragma once

/// \file
/// \brief The events the MQTT interop programs exchange (SWR-MQTT-0014).
///
/// The Go generator in interop/mqtt/go builds the same events, with its own name in every source,
/// so a recorded message says who wrote it. This header builds them for any such name: the
/// producer writes the C++ ones, and the interop suite compares what it reads from the Go goldens
/// with the Go ones.
///
/// Go holds a source in a URI type and writes the non-ASCII bytes of "/événement" percent-encoded;
/// this SDK writes them as they are (D-INTEROP-4). The corpus of "go" spells the source the way Go
/// writes it, because that is the event the Go goldens decode to.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cloudevents/core.hpp>

namespace mqtt_interop {

using namespace ce::literals;

struct named_event {
  std::string name;
  ce::event subject;
};

/// An attribute built from text the corpus controls, so it is valid by construction.
template<class Attribute>
[[nodiscard]] auto attribute_of(std::string_view text) -> Attribute {
  return *Attribute::make(text);
}

[[nodiscard]] inline auto instant(std::string_view text) -> ce::timestamp {
  return *ce::parse_timestamp(text);
}

[[nodiscard]] inline auto corpus(std::string_view producer) -> std::vector<named_event> {
  const std::string origin = "/interop/" + std::string{producer};
  constexpr std::string_view go_producer = "go";
  constexpr std::string_view accented_suffix = "/événement";
  constexpr std::string_view accented_suffix_as_go_writes_it = "/%C3%A9v%C3%A9nement";
  const std::string_view accented =
      producer == go_producer ? accented_suffix_as_go_writes_it : accented_suffix;
  const auto source_of = [&origin](std::string_view suffix = {}) {
    return attribute_of<ce::source>(origin + std::string{suffix});
  };

  return std::vector<named_event>{
      {"minimal", ce::event{"id-minimal"_id, source_of(), "com.example.minimal"_type}},
      {"full",
       ce::event{"id-full"_id,
                 attribute_of<ce::source>("https://example.test/interop/" + std::string{producer}),
                 "com.example.full"_type,
                 {
                     .datacontenttype = "application/json"_mediatype,
                     .dataschema = "https://example.test/schema/1"_dataschema,
                     .subject = "the-subject"_subject,
                     .time = instant("2026-09-20T12:34:56Z"),
                     .data = ce::json_text{.raw = R"({"count":3,"label":")" +
                                                  std::string{producer} + R"("})"},
                 }}},
      {"extensions",
       ce::event{"id-extensions"_id,
                 source_of(),
                 "com.example.extensions"_type,
                 {.extensions =
                      {
                          {"traceparent"_ext,
                           std::string{"00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}},
                          {"sampledrate"_ext, std::int32_t{30}},
                      }}}},
      {"text_data",
       ce::event{
           "id-text"_id,
           source_of(),
           "com.example.text"_type,
           {.datacontenttype = "text/plain"_mediatype, .data = std::string{"plain text payload"}}}},
      {"binary_data",
       ce::event{
           "id-binary"_id,
           source_of(),
           "com.example.binary"_type,
           {
               .datacontenttype = "application/octet-stream"_mediatype,
               .data =
                   ce::binary{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}},
           }}},
      {"extension_types",
       ce::event{"id-types"_id,
                 source_of(),
                 "com.example.types"_type,
                 {.extensions =
                      {
                          {"astring"_ext, std::string{"text"}},
                          {"aninteger"_ext, std::int32_t{42}},
                          {"aboolean"_ext, true},
                          {"negative"_ext, std::int32_t{-7}},
                      }}}},
      {"unicode",
       ce::event{"id-unicode-é文"_id,
                 source_of(accented),
                 "com.example.unicode"_type,
                 {
                     .datacontenttype = "application/json"_mediatype,
                     .subject = "日本語 \U0001F600"_subject,
                     .data = ce::json_text{.raw = "{\"text\":\"é文 \U0001F600\"}"},
                 }}},
      {"data_array",
       ce::event{"id-scalar"_id,
                 source_of(),
                 "com.example.scalar"_type,
                 {.datacontenttype = "application/json"_mediatype,
                  .data = ce::json_text{.raw = "[1,2,3]"}}}},
  };
}

}  // namespace mqtt_interop
