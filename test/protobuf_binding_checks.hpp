#pragma once

/// \file
/// \brief What every binding's Protobuf message entry points promise (SWR-PROTO-0019 to -0021).
///
/// HTTP, Kafka and NATS share one implementation, so one set of checks drives all three through
/// a stand-in that names the binding's own entry points. The stand-in `Bind` has:
///
///   static constexpr std::string_view label          what a failure says
///   static constexpr std::string_view header         the content-type header the binding writes
///   static constexpr bool names_ignore_case          whether the binding finds that header by
///                                                    a case-insensitive name
///   static auto lay_out(const ce::event&) -> ce::result<ce::message>
///   static auto read(const ce::message&, ce::protobuf::decode_options) -> ce::result<ce::event>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "moving_entry_points.hpp"
#include "protobuf_support.hpp"

namespace ce_test::protobuf_binding {

namespace pb = ::ce::test::pb;

inline constexpr std::string_view protobuf_media_type = "application/cloudevents+protobuf";

/// A value no attribute or payload in the samples has, to tell an event from another one.
inline constexpr std::size_t garbage_bytes = 32;
inline constexpr std::size_t one_byte_less = 1;

/// Events that a Protobuf round trip returns unchanged: the four required attributes, every
/// optional attribute, extensions of several types, and each of the payload kinds that keep
/// their type on the wire.
[[nodiscard]] inline auto samples() -> std::vector<ce::event> {
  using namespace ce::literals;
  std::vector<ce::event> out;
  out.push_back(pb::base_event());
  out.push_back(pb::base_event({
      .datacontenttype = "application/octet-stream"_mediatype,
      .dataschema = "https://example.test/schema/1"_dataschema,
      .subject = "the-subject"_subject,
      .extensions = {{"alpha"_ext, std::string{"a"}}, {"beta"_ext, std::int32_t{42}}},
      .data = ce::binary{std::byte{0}, std::byte{1}, std::byte{0xFF}},
  }));
  out.push_back(pb::base_event({
      .datacontenttype = "text/plain"_mediatype,
      .data = std::string{"plain text"},
  }));
  return out;
}

/// A message holding `body`, with `type` as the value of the header `header`.
[[nodiscard]] inline auto message_with(std::string_view header,
                                       std::string_view type,
                                       ce::binary body) -> ce::message {
  return ce::message{.header_fields = {{std::string{header}, std::string{type}}},
                     .body = std::move(body)};
}

template<class Bind>
void lays_out_one_header_and_the_encoded_bytes() {
  using boost::ut::expect;
  for (const auto& subject : samples()) {
    const auto laid_out = Bind::lay_out(subject);
    const auto encoded = ce::protobuf_format::encode(subject);
    expect(laid_out.has_value() && encoded.has_value()) << Bind::label;
    if (!laid_out || !encoded) {
      return;
    }
    expect(laid_out->header_fields.size() == 1U) << Bind::label;
    const std::string* declared = laid_out->header_fields.find_exact(Bind::header);
    expect(declared != nullptr && *declared == protobuf_media_type) << Bind::label;
    expect(laid_out->body == *encoded) << Bind::label;
  }
}

template<class Bind>
void reads_what_it_lays_out() {
  using boost::ut::expect;
  for (const auto& subject : samples()) {
    const auto laid_out = Bind::lay_out(subject);
    expect(laid_out.has_value()) << Bind::label;
    if (!laid_out) {
      return;
    }
    const auto read = Bind::read(*laid_out, {});
    expect(read.has_value()) << Bind::label;
    if (read) {
      expect(bool{*read == subject}) << Bind::label;
    }
  }
}

template<class Bind>
void reads_a_message_another_writer_built() {
  using boost::ut::expect;
  // The goldens' shape: the header and the bytes, assembled here without the writer.
  const auto given = message_with(Bind::header, protobuf_media_type, pb::event_bytes());
  const auto read = Bind::read(given, {});
  expect(read.has_value()) << Bind::label;
  if (read) {
    expect(bool{*read == pb::base_event()}) << Bind::label;
  }
}

template<class Bind>
void refuses_another_content_type() {
  using boost::ut::expect;
  constexpr std::array others{
      std::string_view{"application/json"},
      std::string_view{"application/cloudevents+json"},
      std::string_view{"application/cloudevents+protobufx"},
      std::string_view{"application/cloudevents+protobu"},
      std::string_view{"application/cloudevents-batch+protobuf"},
      std::string_view{"application/protobuf"},
      std::string_view{"x/application/cloudevents+protobuf"},
      std::string_view{"application/cloudevents+protobuf/x"},
      std::string_view{";application/cloudevents+protobuf"},
      std::string_view{""},
      std::string_view{" "},
  };
  for (const std::string_view other : others) {
    const auto given = message_with(Bind::header, other, pb::event_bytes());
    const auto read = Bind::read(given, {});
    expect(!read.has_value() && read.error().code == ce::errc::not_a_cloudevent)
        << Bind::label << ": '" << other << "'";
  }
  const ce::message no_header{.body = pb::event_bytes()};
  const auto missing = Bind::read(no_header, {});
  expect(!missing.has_value() && missing.error().code == ce::errc::not_a_cloudevent)
      << Bind::label << ": no content type";

  // A well-formed event does not make the type right: the check is on the header alone.
  const auto json =
      message_with(Bind::header, "application/cloudevents+json", pb::bytes_of(R"({"id":"1"})"));
  const auto refused_json = Bind::read(json, {});
  expect(!refused_json.has_value() && refused_json.error().code == ce::errc::not_a_cloudevent)
      << Bind::label;
}

template<class Bind>
void ignores_case_and_parameters() {
  using boost::ut::expect;
  constexpr std::array spellings{
      std::string_view{"APPLICATION/CLOUDEVENTS+PROTOBUF"},
      std::string_view{"Application/CloudEvents+Protobuf"},
      std::string_view{"application/cloudevents+protobuf; charset=utf-8"},
      std::string_view{"application/cloudevents+protobuf;charset=utf-8"},
      std::string_view{"application/cloudevents+protobuf ; a=b; c=d"},
      std::string_view{" \tapplication/cloudevents+protobuf \t"},
      std::string_view{"APPLICATION/CLOUDEVENTS+PROTOBUF;Q=1"},
  };
  for (const std::string_view spelled : spellings) {
    const auto given = message_with(Bind::header, spelled, pb::event_bytes());
    const auto read = Bind::read(given, {});
    expect(read.has_value()) << Bind::label << ": '" << spelled << "'";
    if (read) {
      expect(bool{*read == pb::base_event()}) << Bind::label << ": '" << spelled << "'";
    }
  }
}

/// A name that differs from the binding's own in case only: found by a binding whose header
/// names ignore case, and not found by one whose names are exact.
template<class Bind>
void honours_the_binding_rule_for_header_name_case() {
  using boost::ut::expect;
  std::string upper{Bind::header};
  std::string lower{Bind::header};
  for (char& character : upper) {
    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
  }
  for (char& character : lower) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  for (const std::string& spelled : {upper, lower}) {
    const auto given = message_with(spelled, protobuf_media_type, pb::event_bytes());
    const auto read = Bind::read(given, {});
    if (spelled == Bind::header || Bind::names_ignore_case) {
      expect(read.has_value()) << Bind::label << ": " << spelled;
    } else {
      expect(!read.has_value() && read.error().code == ce::errc::not_a_cloudevent)
          << Bind::label << ": " << spelled;
    }
  }
}

template<class Bind>
void passes_the_decode_limits_through() {
  using boost::ut::expect;
  const ce::binary bytes = pb::event_bytes();
  const auto given = message_with(Bind::header, protobuf_media_type, bytes);
  expect(Bind::read(given, {.max_encoded_size = bytes.size()}).has_value()) << Bind::label;
  const auto too_big = Bind::read(given, {.max_encoded_size = bytes.size() - one_byte_less});
  expect(!too_big.has_value() && too_big.error().code == ce::errc::out_of_range) << Bind::label;

  const auto three = message_with(
      Bind::header,
      protobuf_media_type,
      pb::event_bytes(
          {pb::string_entry("a", "1"), pb::string_entry("b", "2"), pb::string_entry("c", "3")}));
  expect(Bind::read(three, {.max_attribute_count = 3}).has_value()) << Bind::label;
  const auto too_many = Bind::read(three, {.max_attribute_count = 2});
  expect(!too_many.has_value() && too_many.error().code == ce::errc::out_of_range) << Bind::label;
}

/// What the format says about the body, unchanged by the binding: the error is the decoder's.
template<class Bind>
void gives_the_decoder_error_for_a_bad_body() {
  using boost::ut::expect;
  const std::array<ce::binary, 3> bodies{
      ce::binary(garbage_bytes, std::byte{0xFF}),
      ce::binary{},
      ce::binary{std::byte{0x0A}, std::byte{0x7F}},
  };
  for (const auto& body : bodies) {
    const auto given = message_with(Bind::header, protobuf_media_type, body);
    const auto read = Bind::read(given, {});
    const auto direct = ce::protobuf_format::decode(body);
    expect(!read.has_value() && !direct.has_value()) << Bind::label;
    if (!read && !direct) {
      expect(read.error().code == direct.error().code) << Bind::label;
    }
  }
  const auto garbage = Bind::read(
      message_with(Bind::header, protobuf_media_type, ce::binary(garbage_bytes, std::byte{0xFF})),
      {});
  expect(!garbage.has_value() && garbage.error().code == ce::errc::parse_error) << Bind::label;
}

/// There is no moving variant: a message handed over with std::move to the one entry point is
/// not adopted from.
template<class Bind>
void leaves_a_moved_message_alone() {
  using boost::ut::expect;
  auto given = message_with(Bind::header, protobuf_media_type, pb::event_bytes());
  const auto before = moving::take(given);
  // NOLINTNEXTLINE(performance-move-const-arg)
  const auto read = Bind::read(std::move(given), {});
  expect(read.has_value()) << Bind::label;
  // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
  expect(moving::unchanged(given, before)) << Bind::label;
}

}  // namespace ce_test::protobuf_binding
