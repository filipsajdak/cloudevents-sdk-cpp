#pragma once

/// \file
/// \brief Binary mode and a `datacontenttype` that names a CloudEvents format (SWR-BIND-0019).
///
/// Binary mode writes `datacontenttype` into the content-type field, which is where every reader
/// looks first to decide the mode. One set of checks drives each binding through a pair of
/// callables, as binding_decode_options.hpp does: `lay_out(event, mode)` is the binding's
/// `to_message` and `read(message)` its `from_message`.

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

namespace ce_test {

/// The payload of the structured MQTT message the scheduled fuzz run of 2026-11-07 minimised its
/// crash to.
inline constexpr std::string_view crash_document =
    R"({"specversion":"1.0","id":"1","source":"/","type":"t","datacontenttype":"application/cloudevents"})";

/// What a relay receives in structured mode: an event whose data is itself an event. Written in
/// binary mode before the fix, it read back as the inner event.
inline constexpr std::string_view relay_document =
    R"({"specversion":"1.0","id":"outer","source":"/relay","type":"t",)"
    R"("datacontenttype":"application/cloudevents+json",)"
    R"("data":{"specversion":"1.0","id":"inner","source":"/elsewhere","type":"other"}})";

/// How a reader's test of a content type treats a media type.
enum class media_type_reading : bool {
  byte_for_byte,  ///< Kafka and MQTT: `starts_with`
  ignoring_case,  ///< HTTP: `starts_with_ignoring_case`
};

struct media_type_case {
  /// The media type as a JSON string spells it, so `\t` is a tab in the event.
  // NOLINTNEXTLINE(custom-scudoai-copy-view-member): every case views a string literal.
  std::string_view datacontenttype;
  bool names_the_format_exactly;        ///< starts with the prefix byte for byte
  bool names_the_format_ignoring_case;  ///< starts with the prefix in some letter case
};

/// The media types a writer must sort as its reader does. The near misses pin the length and the
/// place of the prefix test. Leading whitespace is admitted by the media-type grammar and dropped
/// by an HTTP field parser, so the prefix after it counts: a transport that trims would otherwise
/// deliver a structured message.
inline constexpr std::array media_type_cases{
    media_type_case{" application/cloudevents+json", true, true},
    media_type_case{"\\tapplication/cloudevents+json", true, true},
    media_type_case{" \\t application/cloudevents", true, true},
    media_type_case{"\\u000b\\fapplication/cloudevents+json", true, true},
    media_type_case{"application/cloudevents+json \\t", true, true},
    media_type_case{" Application/CloudEvents+json", false, true},
    media_type_case{" text/plain", false, false},
    media_type_case{"\\ttext/plain; x=application/cloudevents", false, false},
    media_type_case{"application/cloudevents", true, true},
    media_type_case{"application/cloudevents+json", true, true},
    media_type_case{"application/cloudevents+json; charset=utf-8", true, true},
    media_type_case{"application/cloudevents-batch+json", true, true},
    media_type_case{"application/cloudevents+protobuf", true, true},
    media_type_case{"Application/CloudEvents+json", false, true},
    media_type_case{"APPLICATION/CLOUDEVENTS-BATCH+JSON", false, true},
    media_type_case{"application/cloudevent", false, false},
    media_type_case{"application/json", false, false},
    media_type_case{"text/plain; x=application/cloudevents", false, false},
};

/// Media types with a line break before the CloudEvents prefix, as a JSON string spells them. The
/// media-type grammar admits them, but SWR-CORE-0041 makes a CR or LF anywhere invalid, so the
/// structured message carrying them no longer decodes and binary mode never sees them.
inline constexpr std::array media_types_with_a_line_break{
    std::string_view{"\\r\\n application/cloudevents-batch+json"},
    std::string_view{"\\napplication/cloudevents+json"},
    std::string_view{"application/cloudevents+json\\r\\n"},
    std::string_view{"application/cloudevents+json; a=b\\r\\nX-Injected: 1"},
};

[[nodiscard]] inline auto document_declaring(std::string_view datacontenttype) -> std::string {
  return std::string{R"({"specversion":"1.0","id":"1","source":"/","type":"t","datacontenttype":")"}
      .append(datacontenttype)
      .append(R"("})");
}

/// The structured message a binding receives `document` in.
[[nodiscard]] inline auto structured_message(std::string_view content_type_name,
                                             std::string_view document) -> ce::message {
  return ce::message{
      .header_fields = {{std::string{content_type_name}, std::string{ce::json::content_type}}},
      .body = ce::to_bytes(document)};
}

/// True when the two events agree on what a substitution would change.
[[nodiscard]] inline auto same_identity(const ce::event& left, const ce::event& right) -> bool {
  return left.id() == right.id() && left.source() == right.source() &&
         left.type() == right.type() && left.datacontenttype() == right.datacontenttype();
}

/// A received event is refused by binary mode, and still travels in structured mode.
template<class LayOut, class Read>
void expect_refused(std::string_view label, const ce::event& received, LayOut lay_out, Read read) {
  using namespace boost::ut;

  const auto binary = lay_out(received, ce::content_mode::binary_mode);
  expect(!binary.has_value()) << label << ": binary mode produces no message";
  if (!binary) {
    expect(binary.error().code == ce::errc::invalid_argument) << label;
    expect(binary.error().where == "datacontenttype") << label;
  }

  const auto structured = lay_out(received, ce::content_mode::structured);
  expect(structured.has_value()) << label << ": structured mode still writes the event";
  if (structured) {
    const auto back = read(*structured);
    expect(back.has_value() && same_identity(*back, received))
        << label << ": structured mode reads back as the event";
  }
}

/// A received event is written in binary mode and reads back as itself.
template<class LayOut, class Read>
void expect_round_trip(std::string_view label,
                       const ce::event& received,
                       LayOut lay_out,
                       Read read) {
  using namespace boost::ut;

  const auto binary = lay_out(received, ce::content_mode::binary_mode);
  expect(binary.has_value()) << label << ": binary mode writes the event";
  if (binary) {
    const auto back = read(*binary);
    expect(back.has_value() && same_identity(*back, received))
        << label << ": binary mode reads back as the event";
  }
}

/// The minimised crash input, the relay input and every case of `media_type_cases`, received in a
/// structured message under `content_type_name`: binary mode refuses exactly the media types the
/// binding's reader takes for a CloudEvents format, and writes every other one so that it reads
/// back.
template<class LayOut, class Read>
void check_binary_refuses_format_media_type(std::string_view label,
                                            std::string_view content_type_name,
                                            media_type_reading reading,
                                            LayOut lay_out,
                                            Read read) {
  using namespace boost::ut;

  for (const auto document : {crash_document, relay_document}) {
    const auto received = read(structured_message(content_type_name, document));
    expect(received.has_value()) << label << ": " << document;
    if (received) {
      expect_refused(label, *received, lay_out, read);
    }
  }

  for (const auto& sample : media_type_cases) {
    const auto received =
        read(structured_message(content_type_name, document_declaring(sample.datacontenttype)));
    expect(received.has_value()) << label << ": " << sample.datacontenttype;
    if (!received) {
      continue;
    }
    const bool refused = reading == media_type_reading::ignoring_case
                             ? sample.names_the_format_ignoring_case
                             : sample.names_the_format_exactly;
    const std::string case_label = std::string{label}.append(": ").append(sample.datacontenttype);
    if (refused) {
      expect_refused(case_label, *received, lay_out, read);
    } else {
      expect_round_trip(case_label, *received, lay_out, read);
    }
  }

  // A line break makes the media type invalid (SWR-CORE-0041): the structured message is refused.
  for (const auto media_type : media_types_with_a_line_break) {
    const auto received =
        read(structured_message(content_type_name, document_declaring(media_type)));
    expect(!received.has_value()) << label << ": " << media_type;
    if (!received) {
      expect(received.error().code == ce::errc::invalid_content_type)
          << label << ": " << media_type;
      expect(received.error().where == "datacontenttype") << label << ": " << media_type;
    }
  }
}

/// Content types the writer's predicate and the reader's detection must classify alike: every
/// prefix, letter case, parameters, near misses and whitespace the reader does not skip.
inline constexpr std::array content_types_to_agree_on{
    std::string_view{"application/cloudevents"},
    std::string_view{"application/cloudevents+json"},
    std::string_view{"application/cloudevents+json; charset=utf-8"},
    std::string_view{"application/cloudevents-batch"},
    std::string_view{"application/cloudevents-batch+json"},
    std::string_view{"Application/CloudEvents+json"},
    std::string_view{"APPLICATION/CLOUDEVENTS-BATCH+JSON"},
    std::string_view{"application/cloudevent"},
    std::string_view{"application/cloudevents-batc"},
    std::string_view{"application/json"},
    std::string_view{"application/octet-stream"},
    std::string_view{" application/cloudevents+json"},
    std::string_view{"\tapplication/cloudevents-batch+json"},
    std::string_view{"text/plain; x=application/cloudevents"},
    std::string_view{""},
};

/// The writer's `content_mode_of` decides what the reader's `detect_content_mode` decides for a
/// message whose content type, under `content_type_name`, is each of `content_types_to_agree_on`.
template<class ModeOf, class Detect>
void check_writer_agrees_with_reader(std::string_view label,
                                     std::string_view content_type_name,
                                     ModeOf content_mode_of,
                                     Detect detect_content_mode) {
  using namespace boost::ut;

  for (const auto content_type : content_types_to_agree_on) {
    const ce::message received{
        .header_fields = {{std::string{content_type_name}, std::string{content_type}}}};
    expect(content_mode_of(content_type) == detect_content_mode(received))
        << label << ": " << content_type;
  }
}

/// The pin for a binding whose binary mode does not carry `datacontenttype` in its content type
/// (NATS): every case, the crash and the relay input included, writes and reads back.
template<class LayOut, class Read>
void check_binary_keeps_format_media_type(std::string_view label,
                                          std::string_view content_type_name,
                                          LayOut lay_out,
                                          Read read) {
  using namespace boost::ut;

  for (const auto document : {crash_document, relay_document}) {
    const auto received = read(structured_message(content_type_name, document));
    expect(received.has_value()) << label << ": " << document;
    if (received) {
      expect_round_trip(label, *received, lay_out, read);
    }
  }
  for (const auto& sample : media_type_cases) {
    const auto received =
        read(structured_message(content_type_name, document_declaring(sample.datacontenttype)));
    expect(received.has_value()) << label << ": " << sample.datacontenttype;
    if (received) {
      expect_round_trip(
          std::string{label}.append(": ").append(sample.datacontenttype), *received, lay_out, read);
    }
  }
}

/// Content types with a CR, an LF or both at the start, in the parameters and at the end, as a
/// transport would deliver them in the content-type field.
inline constexpr std::array content_types_with_a_line_break{
    std::string_view{"\r\ntext/plain"},
    std::string_view{"\ntext/plain"},
    std::string_view{"text/plain; a=b\r\nX-Injected: 1"},
    std::string_view{"text/plain\r"},
    std::string_view{"text/plain\r\n"},
};

/// A binary-mode message whose content-type field, under `content_type_name`, holds a line break
/// fails `read` with `invalid_content_type` naming `datacontenttype`. `attributes` are the
/// binding's fields for the required attributes.
template<class Read>
void check_reader_refuses_a_line_break(std::string_view label,
                                       const ce::raw_headers& attributes,
                                       std::string_view content_type_name,
                                       Read read) {
  using namespace boost::ut;

  for (const auto content_type : content_types_with_a_line_break) {
    ce::raw_headers fields = attributes;
    fields.set_exact(std::string{content_type_name}, std::string{content_type});
    const auto received = read(ce::message{.header_fields = std::move(fields)});
    expect(!received.has_value()) << label << ": " << content_type;
    if (!received) {
      expect(received.error().code == ce::errc::invalid_content_type)
          << label << ": " << content_type;
      expect(received.error().where == "datacontenttype") << label << ": " << content_type;
    }
  }
}

}  // namespace ce_test
