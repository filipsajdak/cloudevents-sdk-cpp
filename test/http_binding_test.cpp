#include <boost/ut.hpp>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/detail/timestamp.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "codecs_under_test.hpp"
#include "equality.hpp"
#include "mini_codec.hpp"
#include "payload.hpp"

// The binding is a template over the JSON codec, exactly as json_format is. So
// every assertion that touches a codec is written once, as a function template,
// and every suite instantiates it for BOTH in-tree codecs. A suite that
// exercised only nlohmann would be testing nlohmann rather than the binding.
//
// The comparison wrappers `expect(bool{a == b})` are not decoration: ut streams
// both operands when an expectation fails, and ce::uri, ce::uri_ref, their
// std::optional forms and ce::event have no operator<<, so the unwrapped form
// does not compile.

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::codec::nlohmann_codec;
using mini_codec = ce::test::mini_codec;

using namespace ce::literals;

/// A minimal event, built in one expression; the caller states the rest.
[[nodiscard]] auto base_event(ce::event::options rest = {}) -> ce::event {
  return ce::event{"1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

/// A binary-mode message carrying the four attributes every event needs plus the
/// one field a case is about, so a suite can vary one header and still reach the
/// code under test.
[[nodiscard]] auto binary_message_with(ce::raw_headers::entry extra) -> ce::message {
  return ce::message{.header_fields = {{"ce-specversion", "1.0"},
                                       {"ce-id", "1"},
                                       {"ce-source", "/s"},
                                       {"ce-type", "t"},
                                       std::move(extra)}};
}

// Multi-byte UTF-8 written as octets rather than as source characters, so the
// test asserts the same bytes whatever encoding the compiler reads this file in.
constexpr auto two_byte_utf8 = "\xC3\xBC"sv;          // U+00FC
constexpr auto three_byte_utf8 = "\xE6\x97\xA5"sv;    // U+65E5
constexpr auto four_byte_utf8 = "\xF0\x9F\x98\x80"sv; // U+1F600

// --- SWR-HTTP-0003: to_message in a caller-selected content mode -------------

template <class C>
void check_to_message_modes(std::string_view label) {
  using namespace boost::ut;

  const ce::event subject = base_event({.datacontenttype = "application/json"_mediatype,
                                        .data = ce::json_text{.raw = R"({"k":1})"}});

  // The mode is the caller's argument, not a guess from the event contents: the
  // same event lays out two different ways.
  auto structured = ce::http::to_message<C>(subject, ce::content_mode::structured);
  expect(structured.has_value()) << label << ": structured";
  if (structured) {
    const auto* declared = structured->header_fields.find("Content-Type");
    expect(declared != nullptr) << label;
    if (declared != nullptr) {
      expect(*declared == ce::json::content_type) << label << ": " << *declared;
    }
    // The whole event is in the body, so no attribute went into a header.
    expect(structured->header_fields.find("ce-id") == nullptr) << label;
    expect(!structured->body.empty()) << label;
    expect(ce::http::detect_content_mode(*structured) == ce::content_mode::structured) << label;
  }

  auto binary = ce::http::to_message<C>(subject, ce::content_mode::binary_mode);
  expect(binary.has_value()) << label << ": binary";
  if (binary) {
    expect(binary->header_fields.find("ce-id") != nullptr) << label;
    const auto* declared = binary->header_fields.find("Content-Type");
    expect(declared != nullptr) << label;
    if (declared != nullptr) {
      expect(*declared == "application/json"sv) << label << ": " << *declared;
    }
    expect(ce::http::detect_content_mode(*binary) == ce::content_mode::binary_mode) << label;
  }

  // An invalid event never becomes a message, in either mode, because none can be
  // built: the empty id the binding used to refuse is refused where an id is made.
  const auto no_id = ce::id::make(""sv);
  expect(!no_id.has_value()) << label;
  if (!no_id) {
    expect(no_id.error().code == ce::errc::missing_required_attribute) << label;
  }

  // content_mode::batched names an entry point that cannot serve one event, and
  // that is invalid_argument rather than a malformed-event code: nothing about
  // this event is wrong.
  auto wrong_entry_point = ce::http::to_message<C>(subject, ce::content_mode::batched);
  expect(!wrong_entry_point.has_value()) << label;
  if (!wrong_entry_point) {
    expect(wrong_entry_point.error().code == ce::errc::invalid_argument)
        << label << ": " << ce::to_string_view(wrong_entry_point.error().code);
  }
  // The same event through the batch entry point does succeed, so the refusal is
  // about the entry point and not about the event.
  const std::vector<ce::event> one{subject};
  expect(ce::http::to_batch_message<C>(std::span<const ce::event>{one}).has_value()) << label;
}

// --- SWR-HTTP-0004: from_message on receive ----------------------------------

template <class C>
void check_from_message_roundtrip(std::string_view label) {
  using namespace boost::ut;

  const auto parsed_time = ce::parse_timestamp("2018-04-05T17:31:00Z");
  expect(parsed_time.has_value()) << label;
  if (!parsed_time) {
    return;
  }

  const ce::event subject = base_event({
      .datacontenttype = "application/json"_mediatype,
      .dataschema = "https://example.com/schema"_dataschema,
      .subject = "s"_subject,
      .time = *parsed_time,
      .extensions = {{"traceparent"_ext, std::string{"00-abc-def-01"}}},
      .data = ce::json_text{.raw = R"({"k":1})"},
  });

  for (const auto mode : {ce::content_mode::binary_mode, ce::content_mode::structured}) {
    auto request = ce::http::to_message<C>(subject, mode);
    expect(request.has_value()) << label;
    if (!request) {
      continue;
    }
    auto back = ce::http::from_message<C>(*request);
    expect(back.has_value()) << label;
    if (!back) {
      expect(false) << label << ": " << back.error().detail;
      continue;
    }
    expect(back->id() == subject.id()) << label;
    expect(back->type() == subject.type()) << label;
    expect(back->specversion().view() == "1.0"sv) << label;
    expect(bool{back->source() == subject.source()}) << label;
    expect(ce_test::equal(back->subject(), subject.subject())) << label;
    expect(ce_test::equal(back->dataschema(), subject.dataschema())) << label;
    expect(ce_test::equal(back->datacontenttype(), subject.datacontenttype())) << label;
    expect(back->time().has_value()) << label;
    if (back->time()) {
      expect(ce::to_string(*back->time()) == "2018-04-05T17:31:00Z"sv) << label;
    }
    expect(back->extensions().size() == 1U) << label;
    expect(back->extension("traceparent") != nullptr) << label;
  }

  // Failure is a returned value with a code, never an exception: the receive path
  // parses bytes an attacker chose.
  static_assert(std::is_same_v<decltype(ce::http::from_message<C>(std::declval<const ce::message&>())),
                               ce::result<ce::event>>);

  // A malformed structured body fails as a parse error rather than half-decoding.
  const ce::message broken{
      .header_fields = {{"Content-Type", std::string{ce::json::content_type}}},
      .body = ce::http::detail::to_bytes("{"),
  };
  auto refused = ce::http::from_message<C>(broken);
  expect(!refused.has_value()) << label;
  if (!refused) {
    expect(refused.error().code == ce::errc::parse_error) << label;
  }

  // A binary-mode message missing a required attribute names the one that is
  // absent, so a caller can say what the peer got wrong.
  const ce::message incomplete{
      .header_fields = {{"ce-specversion", "1.0"}, {"ce-source", "/s"}, {"ce-type", "t"}}};
  auto missing = ce::http::from_message<C>(incomplete);
  expect(!missing.has_value()) << label;
  if (!missing) {
    expect(missing.error().code == ce::errc::missing_required_attribute) << label;
    expect(missing.error().where == "id"sv) << label << ": " << missing.error().where;
  }

  // A specversion this SDK does not implement is its own code, not a parse error.
  const ce::message future{.header_fields = {{"ce-specversion", "0.3"},
                                             {"ce-id", "1"},
                                             {"ce-source", "/s"},
                                             {"ce-type", "t"}}};
  auto unsupported = ce::http::from_message<C>(future);
  expect(!unsupported.has_value()) << label;
  if (!unsupported) {
    expect(unsupported.error().code == ce::errc::unsupported_spec_version) << label;
  }

  // Header names arrive in whatever case the peer sent, because HTTP field names
  // are case-insensitive and intermediaries do rewrite them.
  const ce::message mixed_case{.header_fields = {{"CE-SpecVersion", "1.0"},
                                                 {"Ce-Id", "7"},
                                                 {"ce-SOURCE", "/s"},
                                                 {"CE-type", "t"}}};
  auto decoded = ce::http::from_message<C>(mixed_case);
  expect(decoded.has_value()) << label;
  if (decoded) {
    expect(decoded->id().view() == "7"sv) << label;
    expect(decoded->type().view() == "t"sv) << label;
    expect(decoded->source().view() == "/s"sv) << label;
  }
}

// --- SWR-HTTP-0005: the batched variants -------------------------------------

template <class C>
void check_batched_variants(std::string_view label) {
  using namespace boost::ut;

  const std::vector<ce::event> many{base_event(), base_event()};
  auto request = ce::http::to_batch_message<C>(std::span<const ce::event>{many});
  expect(request.has_value()) << label;
  if (!request) {
    return;
  }
  const auto* declared = request->header_fields.find("Content-Type");
  expect(declared != nullptr) << label;
  if (declared != nullptr) {
    expect(*declared == ce::json::batch_content_type) << label << ": " << *declared;
  }
  expect(ce::http::detect_content_mode(*request) == ce::content_mode::batched) << label;

  auto back = ce::http::from_batch_message<C>(*request);
  expect(back.has_value()) << label;
  if (back) {
    expect(back->size() == 2U) << label;
    for (const auto& element : *back) {
      expect(bool{element == base_event()}) << label << ": batch element changed";
    }
  }

  // No events is a batch too, and it survives the round trip as no events.
  const std::vector<ce::event> none;
  auto empty_request = ce::http::to_batch_message<C>(std::span<const ce::event>{none});
  expect(empty_request.has_value()) << label;
  if (empty_request) {
    auto empty_back = ce::http::from_batch_message<C>(*empty_request);
    expect(empty_back.has_value()) << label;
    if (empty_back) {
      expect(empty_back->empty()) << label;
    }
  }

  // A message that is not a batch does not silently decode as one.
  auto structured = ce::http::to_message<C>(base_event(), ce::content_mode::structured);
  expect(structured.has_value()) << label;
  if (structured) {
    auto not_a_batch = ce::http::from_batch_message<C>(*structured);
    expect(!not_a_batch.has_value()) << label;
    if (!not_a_batch) {
      expect(not_a_batch.error().code == ce::errc::not_a_cloudevent) << label;
    }
  }

  // The mirror of the to_message refusal: asking the single-event decoder for a
  // batch is invalid_argument, because the caller named the wrong entry point
  // rather than because the message is broken.
  auto wrong_entry_point = ce::http::from_message<C>(*request);
  expect(!wrong_entry_point.has_value()) << label;
  if (!wrong_entry_point) {
    expect(wrong_entry_point.error().code == ce::errc::invalid_argument)
        << label << ": " << ce::to_string_view(wrong_entry_point.error().code);
  }
}

// --- SWR-HTTP-0006: content mode detection -----------------------------------

template <class C>
void check_detection_of_produced_messages(std::string_view label) {
  using namespace boost::ut;

  auto binary = ce::http::to_message<C>(base_event(), ce::content_mode::binary_mode);
  expect(binary.has_value()) << label;
  if (binary) {
    expect(ce::http::detect_content_mode(*binary) == ce::content_mode::binary_mode) << label;
  }
  auto structured = ce::http::to_message<C>(base_event(), ce::content_mode::structured);
  expect(structured.has_value()) << label;
  if (structured) {
    expect(ce::http::detect_content_mode(*structured) == ce::content_mode::structured) << label;
  }
  const std::vector<ce::event> many{base_event()};
  auto batched = ce::http::to_batch_message<C>(std::span<const ce::event>{many});
  expect(batched.has_value()) << label;
  if (batched) {
    expect(ce::http::detect_content_mode(*batched) == ce::content_mode::batched) << label;
  }
}

// --- SWR-HTTP-0007: binary mode carries attributes as ce- headers -------------

template <class C>
void check_binary_mode_ce_headers(std::string_view label) {
  using namespace boost::ut;

  const auto parsed_time = ce::parse_timestamp("2018-04-05T17:31:00Z");
  expect(parsed_time.has_value()) << label;
  if (!parsed_time) {
    return;
  }

  const ce::event subject = base_event({
      .dataschema = "https://example.com/schema"_dataschema,
      .subject = "s"_subject,
      .time = *parsed_time,
      .extensions = {{"traceparent"_ext, std::string{"00-abc"}}, {"seq"_ext, std::int32_t{7}}},
  });

  auto request = ce::http::to_message<C>(subject, ce::content_mode::binary_mode);
  expect(request.has_value()) << label;
  if (!request) {
    return;
  }

  const std::pair<std::string_view, std::string_view> expected[] = {
      {"ce-specversion"sv, "1.0"sv},
      {"ce-id"sv, "1"sv},
      {"ce-source"sv, "/spec/test"sv},
      {"ce-type"sv, "com.example.thing"sv},
      {"ce-dataschema"sv, "https://example.com/schema"sv},
      {"ce-subject"sv, "s"sv},
      {"ce-time"sv, "2018-04-05T17:31:00Z"sv},
      // Extensions take the same mapping as the context attributes.
      {"ce-traceparent"sv, "00-abc"sv},
      {"ce-seq"sv, "7"sv},
  };
  for (const auto& [name, value] : expected) {
    const auto* found = request->header_fields.find(name);
    expect(found != nullptr) << label << ": " << name;
    if (found != nullptr) {
      expect(*found == value) << label << ": " << name << " is " << *found;
    }
  }

  // The name is emitted in lower case, not merely findable case-insensitively:
  // an intermediary matching bytes must see what the binding promises.
  for (const auto& [name, value] : request->header_fields) {
    if (!ce::http::detail::starts_with_ignoring_case(name, "ce-")) {
      continue;
    }
    std::string lowered = name;
    for (char& character : lowered) {
      character = ce::detail::ascii_lower(character);
    }
    expect(lowered == name) << label << ": header name is not lower case: " << name;
  }

  // An attribute the event does not carry emits no header at all, rather than an
  // empty one a receiver would read as a present-but-empty attribute.
  auto bare = ce::http::to_message<C>(base_event(), ce::content_mode::binary_mode);
  expect(bare.has_value()) << label;
  if (bare) {
    for (const auto absent : {"ce-subject"sv, "ce-time"sv, "ce-dataschema"sv}) {
      expect(bare->header_fields.find(absent) == nullptr) << label << ": " << absent;
    }
  }
}

// --- SWR-HTTP-0008: datacontenttype maps to Content-Type ---------------------

template <class C>
void check_datacontenttype_mapping(std::string_view label) {
  using namespace boost::ut;

  for (const auto media_type : {"application/json"sv, "text/plain"sv, "application/xml"sv,
                                "text/plain; charset=utf-8"sv}) {
    const auto made = ce::datacontenttype::make(media_type);
    expect(made.has_value()) << label << ": " << media_type;
    if (!made) {
      continue;
    }
    const ce::event subject = base_event({.datacontenttype = *made});

    auto request = ce::http::to_message<C>(subject, ce::content_mode::binary_mode);
    expect(request.has_value()) << label << ": " << media_type;
    if (!request) {
      continue;
    }
    const auto* declared = request->header_fields.find("Content-Type");
    expect(declared != nullptr) << label << ": " << media_type;
    if (declared != nullptr) {
      expect(*declared == media_type) << label << ": " << *declared;
    }
    // Emitting it in both places would let a receiver read two media types for
    // one body, so the ce- form must be absent rather than merely consistent.
    expect(request->header_fields.find("ce-datacontenttype") == nullptr)
        << label << ": " << media_type;

    // And the value comes back as datacontenttype rather than as an extension.
    auto back = ce::http::from_message<C>(*request);
    expect(back.has_value()) << label << ": " << media_type;
    if (back) {
      expect(ce_test::equal(back->datacontenttype(), subject.datacontenttype())) << label;
      expect(back->extension("datacontenttype") == nullptr) << label;
      expect(back->extensions().empty()) << label;
    }
  }

  // With no datacontenttype the binding invents no Content-Type either.
  auto bare = ce::http::to_message<C>(base_event(), ce::content_mode::binary_mode);
  expect(bare.has_value()) << label;
  if (bare) {
    expect(bare->header_fields.find("Content-Type") == nullptr) << label;
    expect(bare->header_fields.find("ce-datacontenttype") == nullptr) << label;
  }
}

// --- SWR-HTTP-0009: the binary-mode body is the data itself -------------------

template <class C>
void check_binary_mode_body(std::string_view label) {
  using namespace boost::ut;

  // Bytes: the body is those octets, not a base64 or JSON rendering of them. A
  // consumer that knows nothing of CloudEvents reads what the producer set.
  const ce::binary octets{std::byte{0x00}, std::byte{0x01}, std::byte{0xFF}, std::byte{0x10}};
  const ce::event with_bytes = base_event({.data = octets});
  auto from_bytes = ce::http::to_message<C>(with_bytes, ce::content_mode::binary_mode);
  expect(from_bytes.has_value()) << label;
  if (from_bytes) {
    expect(from_bytes->body == octets) << label;
    auto back = ce::http::from_message<C>(*from_bytes);
    expect(back.has_value()) << label;
    if (back) {
      expect(std::holds_alternative<ce::binary>(back->data())) << label;
      if (std::holds_alternative<ce::binary>(back->data())) {
        expect(std::get<ce::binary>(back->data()) == octets) << label;
      }
    }
  }

  // Text: the same bytes, not a JSON string with escapes added.
  const ce::event with_text = base_event(
      {.datacontenttype = "text/plain"_mediatype, .data = std::string{R"(hello "world")"}});
  auto from_text = ce::http::to_message<C>(with_text, ce::content_mode::binary_mode);
  expect(from_text.has_value()) << label;
  if (from_text) {
    expect(from_text->body == ce::http::detail::to_bytes(R"(hello "world")")) << label;
    expect(ce::http::detail::to_text(from_text->body) == R"(hello "world")"sv) << label;
  }

  // Pre-serialized JSON: the raw text, byte for byte, with no reformatting.
  constexpr auto raw = R"({"k":1,"list":[1,2]})"sv;
  const ce::event with_json = base_event({.datacontenttype = "application/json"_mediatype,
                                          .data = ce::json_text{.raw = std::string{raw}}});
  auto from_json = ce::http::to_message<C>(with_json, ce::content_mode::binary_mode);
  expect(from_json.has_value()) << label;
  if (from_json) {
    expect(ce::http::detail::to_text(from_json->body) == raw) << label;
    auto back = ce::http::from_message<C>(*from_json);
    expect(back.has_value()) << label;
    if (back && std::holds_alternative<ce::json_text>(back->data())) {
      expect(std::get<ce::json_text>(back->data()).raw == raw) << label;
    }
  }

  // No payload leaves the body empty rather than writing a placeholder.
  auto bare = ce::http::to_message<C>(base_event(), ce::content_mode::binary_mode);
  expect(bare.has_value()) << label;
  if (bare) {
    expect(bare->body.empty()) << label;
    auto back = ce::http::from_message<C>(*bare);
    expect(back.has_value()) << label;
    if (back) {
      expect(std::holds_alternative<std::monostate>(back->data())) << label;
    }
  }
}

constexpr auto document_payload = R"({"a":[1,2],"b":{"c":null}})"sv;

template <class C>
void check_document_round_trip(std::string_view label) {
  using namespace boost::ut;

  const auto parsed = C::parse(document_payload);
  expect(parsed.has_value()) << label;
  if (!parsed) {
    return;
  }
  const ce::event subject = base_event({
      .datacontenttype = "application/json"_mediatype,
      .data = ce::json_document::make<C>(C::copy(*parsed)),
  });

  for (const auto mode : {ce::content_mode::structured, ce::content_mode::binary_mode}) {
    auto laid_out = ce::http::to_message<C>(subject, mode);
    expect(laid_out.has_value()) << label;
    if (!laid_out) {
      continue;
    }
    auto back = ce::http::from_message<C>(*laid_out);
    expect(back && ce_test::same_json_payload<C>(back->data(), document_payload)) << label;
    if (mode == ce::content_mode::binary_mode) {
      expect(back && std::holds_alternative<ce::json_text>(back->data()))
          << label << ": binary mode reads JSON text";
    }
  }
}

// --- SWR-HTTP-0010: percent-encoding on send ---------------------------------

template <class C>
void check_percent_encoding_roundtrip(std::string_view label) {
  using namespace boost::ut;

  const std::string awkward = std::string{"a b "} + std::string{R"("quoted")"} + " 100% " +
                              std::string{two_byte_utf8} + " " + std::string{three_byte_utf8} +
                              " " + std::string{four_byte_utf8};

  const auto awkward_subject = ce::subject::make(awkward);
  expect(awkward_subject.has_value()) << label;
  if (!awkward_subject) {
    return;
  }
  const ce::event subject =
      base_event({.subject = *awkward_subject, .extensions = {{"trace"_ext, awkward}}});

  auto request = ce::http::to_message<C>(subject, ce::content_mode::binary_mode);
  expect(request.has_value()) << label;
  if (!request) {
    return;
  }

  for (const auto name : {"ce-subject"sv, "ce-trace"sv}) {
    const auto* encoded = request->header_fields.find(name);
    expect(encoded != nullptr) << label << ": " << name;
    if (encoded == nullptr) {
      continue;
    }
    // A header value has to be printable ASCII on the wire, so nothing outside
    // 0x21..0x7E may survive, and the three specials must be gone as well.
    expect(encoded->find(' ') == std::string::npos) << label << ": " << *encoded;
    expect(encoded->find('"') == std::string::npos) << label << ": " << *encoded;
    for (const char character : *encoded) {
      const auto byte = static_cast<unsigned char>(character);
      expect(byte >= 0x21 && byte <= 0x7E) << label << ": non-printable octet in " << *encoded;
    }
    // Each class of input named by the requirement has its escape in the output.
    for (const auto escape : {"%20"sv, "%22"sv, "%25"sv, "%C3%BC"sv, "%E6%97%A5"sv,
                              "%F0%9F%98%80"sv}) {
      expect(encoded->find(escape) != std::string::npos)
          << label << ": " << escape << " missing from " << *encoded;
    }
  }

  // The value the peer reads back is the value that was set, octet for octet.
  auto back = ce::http::from_message<C>(*request);
  expect(back.has_value()) << label;
  if (back) {
    expect(ce_test::equal(back->subject(), subject.subject())) << label;
    const auto* trace = back->extension("trace");
    expect(trace != nullptr) << label;
    if (trace != nullptr && std::holds_alternative<std::string>(*trace)) {
      expect(std::get<std::string>(*trace) == awkward) << label;
    }
  }

  // Printable ASCII that needs no escaping is left alone, so the encoding is
  // minimal rather than blanket: a reader can still see what the value says.
  const ce::event plain = base_event({.subject = "com.example/path?a=1&b=2"_subject});
  auto plain_request = ce::http::to_message<C>(plain, ce::content_mode::binary_mode);
  expect(plain_request.has_value()) << label;
  if (plain_request) {
    const auto* encoded = plain_request->header_fields.find("ce-subject");
    expect(encoded != nullptr) << label;
    if (encoded != nullptr) {
      expect(*encoded == "com.example/path?a=1&b=2"sv) << label << ": " << *encoded;
    }
  }
}

// --- SWR-HTTP-0011: percent-decoding on receive ------------------------------

template <class C>
void check_percent_decoding(std::string_view label) {
  using namespace boost::ut;

  // Another SDK may escape octets this one would have left literal, and a
  // receiver that rejected those would be unable to talk to it.
  const std::pair<std::string_view, std::string_view> accepted[] = {
      {"%41"sv, "A"sv},                                // never needed escaping
      {"%41%42%43"sv, "ABC"sv},
      {"a%2Fb"sv, "a/b"sv},                            // a reserved character, escaped
      {"%63%6F%6D%2E%65%78%61%6D%70%6C%65"sv, "com.example"sv},
      {"%25"sv, "%"sv},                                // the escape character itself
      {"%20"sv, " "sv},
      {"%c3%bc"sv, two_byte_utf8},                     // lower-case hex digits
      {"%C3%BC"sv, two_byte_utf8},
      {"%F0%9F%98%80"sv, four_byte_utf8},
      {"plain"sv, "plain"sv},                          // nothing to decode
  };

  for (const auto& [encoded, decoded] : accepted) {
    auto back =
        ce::http::from_message<C>(binary_message_with({"ce-subject", std::string{encoded}}));
    expect(back.has_value()) << label << ": " << encoded;
    if (!back) {
      continue;
    }
    expect(back->subject().has_value()) << label << ": " << encoded;
    if (back->subject()) {
      expect(back->subject()->view() == decoded)
          << label << ": " << encoded << " gave " << back->subject()->view();
    }
  }

  // An escape that runs off the end of the value, and one whose digits are not
  // hexadecimal, are both parse errors rather than a silent partial decode.
  for (const auto broken : {"abc%4"sv, "abc%"sv, "%"sv, "%ZZ"sv, "%4Z"sv, "%G0"sv}) {
    auto refused =
        ce::http::from_message<C>(binary_message_with({"ce-subject", std::string{broken}}));
    expect(!refused.has_value()) << label << ": should reject " << broken;
    if (!refused) {
      expect(refused.error().code == ce::errc::parse_error)
          << label << ": " << broken << " gave " << ce::to_string_view(refused.error().code);
    }
  }
}

// --- SWR-HTTP-0012: invalid UTF-8 after decoding is an error ------------------

template <class C>
void check_invalid_utf8_rejected(std::string_view label) {
  using namespace boost::ut;

  // Each of these decodes to octets that are not well-formed UTF-8. Admitting any
  // of them would put bytes into a std::string attribute that JSON encoding and
  // logging downstream cannot represent.
  const std::pair<std::string_view, std::string_view> ill_formed[] = {
      {"%C0%80"sv, "overlong two-byte NUL"sv},
      {"%E0%80%AF"sv, "overlong three-byte solidus"sv},
      {"%ED%A0%80"sv, "UTF-16 surrogate D800"sv},
      {"%ED%BF%BF"sv, "UTF-16 surrogate DFFF"sv},
      {"%F5%80%80%80"sv, "above U+10FFFF"sv},
      {"%E2%82"sv, "truncated three-byte sequence"sv},
      {"%F0%9F%98"sv, "truncated four-byte sequence"sv},
      {"%80"sv, "stray continuation byte"sv},
      {"%FF%FE"sv, "not a lead byte at all"sv},
  };

  for (const auto& [encoded, why] : ill_formed) {
    auto refused =
        ce::http::from_message<C>(binary_message_with({"ce-subject", std::string{encoded}}));
    expect(!refused.has_value()) << label << ": should reject " << why;
    if (!refused) {
      expect(refused.error().code == ce::errc::invalid_utf8)
          << label << ": " << why << " gave " << ce::to_string_view(refused.error().code);
      // The error names the attribute, so a caller can report which header the
      // peer got wrong rather than "somewhere in the request".
      expect(refused.error().where == "subject"sv) << label << ": " << refused.error().where;
    }
  }

  // The same check guards an extension header, not only the context attributes.
  auto refused_extension = ce::http::from_message<C>(binary_message_with({"ce-trace", "%C0%80"}));
  expect(!refused_extension.has_value()) << label;
  if (!refused_extension) {
    expect(refused_extension.error().code == ce::errc::invalid_utf8) << label;
  }

  // Well-formed multi-byte text is accepted, so the rejections above are about
  // ill-formedness and not about non-ASCII generally.
  auto accepted = ce::http::from_message<C>(
      binary_message_with({"ce-subject", "%C3%BC%E6%97%A5%F0%9F%98%80"}));
  expect(accepted.has_value()) << label;
  if (accepted && accepted->subject()) {
    const std::string expected =
        std::string{two_byte_utf8} + std::string{three_byte_utf8} + std::string{four_byte_utf8};
    expect(accepted->subject()->view() == expected) << label;
  }
}

// --- SWR-HTTP-0013: binary-mode extensions decode as strings ------------------

template <class C>
void check_extension_string_type(std::string_view label) {
  using namespace boost::ut;

  const auto parsed_time = ce::parse_timestamp("2018-04-05T17:31:00Z");
  expect(parsed_time.has_value()) << label;
  if (!parsed_time) {
    return;
  }

  const ce::event subject = base_event({.extensions = {
                                            {"seq"_ext, std::int32_t{7}},
                                            {"flag"_ext, true},
                                            {"off"_ext, false},
                                            {"tag"_ext, std::string{"x"}},
                                            {"schemaurl"_ext, ce::uri{"https://example.com/x"}},
                                            {"relref"_ext, ce::uri_ref{"/relative"}},
                                            {"seen"_ext, *parsed_time},
                                        }});

  auto request = ce::http::to_message<C>(subject, ce::content_mode::binary_mode);
  expect(request.has_value()) << label;
  if (!request) {
    return;
  }
  auto back = ce::http::from_message<C>(*request);
  expect(back.has_value()) << label;
  if (!back) {
    return;
  }

  // A header carries text and nothing else, so the receiver cannot know whether
  // the peer meant a boolean, an integer, a URI or a timestamp. Every extension
  // therefore arrives as std::string. The typed extension structs of SPEC 5.5
  // (M5) are what recover the declared type; the binding must not guess it.
  const std::pair<std::string_view, std::string_view> expected[] = {
      {"seq"sv, "7"sv},
      {"flag"sv, "true"sv},
      {"off"sv, "false"sv},
      {"tag"sv, "x"sv},
      {"schemaurl"sv, "https://example.com/x"sv},
      {"relref"sv, "/relative"sv},
      {"seen"sv, "2018-04-05T17:31:00Z"sv},
  };
  expect(back->extensions().size() == std::size(expected)) << label;
  for (const auto& [name, rendered] : expected) {
    const auto* held = back->extension(name);
    expect(held != nullptr) << label << ": " << name;
    if (held == nullptr) {
      continue;
    }
    expect(std::holds_alternative<std::string>(*held)) << label << ": " << name;
    // Specifically NOT the alternative it was set as: the wire form lost it.
    expect(!std::holds_alternative<std::int32_t>(*held)) << label << ": " << name;
    expect(!std::holds_alternative<bool>(*held)) << label << ": " << name;
    if (std::holds_alternative<std::string>(*held)) {
      // The text is lossless even though the type is not, so a typed extension
      // struct can parse the declared type back out of it.
      expect(std::get<std::string>(*held) == rendered)
          << label << ": " << name << " is " << std::get<std::string>(*held);
    }
  }
}

// --- SWR-HTTP-0014: not_a_cloudevent is distinct from malformed ---------------

template <class C>
void check_not_a_cloudevent(std::string_view label) {
  using namespace boost::ut;

  // A request that was never an event: a server wants to hand this to its own
  // routing, which it can only do if the code says so.
  const std::pair<std::string_view, std::string_view> passers_by[] = {
      {"text/plain"sv, "hello"sv},
      {"text/html"sv, "<html></html>"sv},
      {"application/json"sv, R"({"k":1})"sv},
      {"application/x-www-form-urlencoded"sv, "a=1"sv},
  };
  for (const auto& [media_type, body] : passers_by) {
    const ce::message request{.header_fields = {{"Content-Type", std::string{media_type}}},
                              .body = ce::http::detail::to_bytes(body)};
    auto refused = ce::http::from_message<C>(request);
    expect(!refused.has_value()) << label << ": " << media_type;
    if (!refused) {
      expect(refused.error().code == ce::errc::not_a_cloudevent)
          << label << ": " << media_type << " gave " << ce::to_string_view(refused.error().code);
    }
  }

  // No Content-Type at all is the binary mode, and without ce-specversion it is
  // still a passer-by rather than a broken event.
  const ce::message bare{};
  auto bare_refused = ce::http::from_message<C>(bare);
  expect(!bare_refused.has_value()) << label;
  if (!bare_refused) {
    expect(bare_refused.error().code == ce::errc::not_a_cloudevent) << label;
  }
  // Even carrying unrelated headers, as long as none of them is ce-specversion.
  const ce::message unrelated{
      .header_fields = {{"Authorization", "Bearer x"}, {"X-Request-Id", "42"}}};
  auto unrelated_refused = ce::http::from_message<C>(unrelated);
  expect(!unrelated_refused.has_value()) << label;
  if (!unrelated_refused) {
    expect(unrelated_refused.error().code == ce::errc::not_a_cloudevent) << label;
  }

  // The other side of the contract: a message that IS a CloudEvent and is broken
  // must NOT report not_a_cloudevent. Conflating the two changes what a receiver
  // does -- pass the request on, or answer the peer that its event is wrong.
  const ce::message missing_id{
      .header_fields = {{"ce-specversion", "1.0"}, {"ce-source", "/s"}, {"ce-type", "t"}}};
  auto broken_binary = ce::http::from_message<C>(missing_id);
  expect(!broken_binary.has_value()) << label;
  if (!broken_binary) {
    expect(broken_binary.error().code == ce::errc::missing_required_attribute) << label;
    expect(broken_binary.error().code != ce::errc::not_a_cloudevent) << label;
  }

  const ce::message broken_structured{
      .header_fields = {{"Content-Type", std::string{ce::json::content_type}}},
      .body = ce::http::detail::to_bytes(R"({"specversion":)"),
  };
  auto structured_error = ce::http::from_message<C>(broken_structured);
  expect(!structured_error.has_value()) << label;
  if (!structured_error) {
    expect(structured_error.error().code == ce::errc::parse_error) << label;
    expect(structured_error.error().code != ce::errc::not_a_cloudevent) << label;
  }

  auto time_error = ce::http::from_message<C>(binary_message_with({"ce-time", "not-a-timestamp"}));
  expect(!time_error.has_value()) << label;
  if (!time_error) {
    expect(time_error.error().code != ce::errc::not_a_cloudevent) << label;
  }

  // And the positive control: with ce-specversion present under a content type
  // that selects the binary mode, the same shape of request decodes.
  const ce::message good{.header_fields = {{"ce-specversion", "1.0"},
                                           {"ce-id", "1"},
                                           {"ce-source", "/s"},
                                           {"ce-type", "t"},
                                           {"Content-Type", "text/plain"}},
                         .body = ce::http::detail::to_bytes("hello")};
  auto accepted = ce::http::from_message<C>(good);
  expect(accepted.has_value()) << label;
  if (accepted) {
    expect(accepted->id().view() == "1"sv) << label;
  }
}

/// A structured or batched body is read through a view of its bytes, so the view
/// must cover every byte, bytes above 0x7F included, and an empty body must still
/// reach the parser and be refused by it.
template <class C>
void check_structured_body_is_read_in_place(std::string_view codec) {
  using namespace boost::ut;
  const std::string label{codec};

  const ce::message empty_structured{
      .header_fields = {{"Content-Type", std::string{ce::json::content_type}}}};
  auto empty_event = ce::http::from_message<C>(empty_structured);
  expect(!empty_event.has_value()) << label;
  if (!empty_event) {
    expect(empty_event.error().code == ce::errc::parse_error) << label;
  }

  const ce::message empty_batch{
      .header_fields = {{"Content-Type", std::string{ce::json::batch_content_type}}}};
  auto empty_events = ce::http::from_batch_message<C>(empty_batch);
  expect(!empty_events.has_value()) << label;
  if (!empty_events) {
    expect(empty_events.error().code == ce::errc::parse_error) << label;
  }

  const ce::message non_ascii{
      .header_fields = {{"Content-Type", std::string{ce::json::content_type}}},
      .body = ce::http::detail::to_bytes(
          R"({"specversion":"1.0","id":"1","source":"/s","type":"t","subject":"zażółć"})"),
  };
  auto decoded = ce::http::from_message<C>(non_ascii);
  expect(decoded.has_value() && decoded->subject().has_value()) << label;
  if (decoded && decoded->subject()) {
    expect(decoded->subject()->view() == "zażółć"sv) << label;
  }
}

// --- SYS-HTTP-0001: the HTTP binding specification's own examples -------------

/// The structured-mode document from the CloudEvents HTTP protocol binding,
/// section 3.2.4.
constexpr auto spec_structured_body = R"({
    "specversion" : "1.0",
    "type" : "com.example.someevent",
    "source" : "/mycontext/subcontext",
    "id" : "1234-1234-1234",
    "time" : "2018-04-05T03:56:24Z",
    "exampleextension1" : "value",
    "datacontenttype" : "application/json",
    "data" : {"much":"wow"}
})"sv;

template <class C>
void check_spec_examples(std::string_view label) {
  using namespace boost::ut;

  // Binary mode, section 3.1.5: the attributes are headers, the payload is the
  // body, and the header names arrive lower case as an HTTP peer sends them.
  const ce::message binary_request{
      .header_fields = {{"ce-specversion", "1.0"},
                        {"ce-type", "com.example.someevent"},
                        {"ce-time", "2018-04-05T03:56:24Z"},
                        {"ce-id", "1234-1234-1234"},
                        {"ce-source", "/mycontext/subcontext"},
                        {"content-type", "application/json"},
                        {"ce-exampleextension1", "value"},
                        {"ce-exampleextension2", "5"}},
      .body = ce::http::detail::to_bytes(R"({"much":"wow"})"),
  };

  expect(ce::http::detect_content_mode(binary_request) == ce::content_mode::binary_mode) << label;
  auto from_binary = ce::http::from_message<C>(binary_request);
  expect(from_binary.has_value()) << label;
  if (!from_binary) {
    expect(false) << label << ": " << from_binary.error().detail;
    return;
  }
  expect(from_binary->specversion().view() == "1.0"sv) << label;
  expect(from_binary->id().view() == "1234-1234-1234"sv) << label;
  expect(from_binary->type().view() == "com.example.someevent"sv) << label;
  expect(from_binary->source().view() == "/mycontext/subcontext"sv) << label;
  expect(from_binary->datacontenttype().has_value() &&
         from_binary->datacontenttype()->view() == "application/json"sv)
      << label;
  expect(from_binary->time().has_value()) << label;
  if (from_binary->time()) {
    expect(ce::to_string(*from_binary->time()) == "2018-04-05T03:56:24Z"sv) << label;
  }
  expect(from_binary->extensions().size() == 2U) << label;
  expect(std::holds_alternative<ce::json_text>(from_binary->data())) << label;
  if (std::holds_alternative<ce::json_text>(from_binary->data())) {
    expect(std::get<ce::json_text>(from_binary->data()).raw == R"({"much":"wow"})"sv) << label;
  }

  // Sending the decoded event back out reproduces the example's headers, so the
  // two directions agree on the same wire form.
  auto re_sent = ce::http::to_message<C>(*from_binary, ce::content_mode::binary_mode);
  expect(re_sent.has_value()) << label;
  if (re_sent) {
    const std::pair<std::string_view, std::string_view> expected[] = {
        {"ce-specversion"sv, "1.0"sv},
        {"ce-type"sv, "com.example.someevent"sv},
        {"ce-time"sv, "2018-04-05T03:56:24Z"sv},
        {"ce-id"sv, "1234-1234-1234"sv},
        {"ce-source"sv, "/mycontext/subcontext"sv},
        {"Content-Type"sv, "application/json"sv},
        {"ce-exampleextension1"sv, "value"sv},
        {"ce-exampleextension2"sv, "5"sv},
    };
    for (const auto& [name, value] : expected) {
      const auto* found = re_sent->header_fields.find(name);
      expect(found != nullptr) << label << ": " << name;
      if (found != nullptr) {
        expect(*found == value) << label << ": " << name << " is " << *found;
      }
    }
    expect(ce::http::detail::to_text(re_sent->body) == R"({"much":"wow"})"sv) << label;
  }

  // Structured mode, section 3.2.4: the same event, wholly in the body.
  const ce::message structured_request{
      .header_fields = {{"content-type", "application/cloudevents+json; charset=UTF-8"}},
      .body = ce::http::detail::to_bytes(spec_structured_body),
  };
  expect(ce::http::detect_content_mode(structured_request) == ce::content_mode::structured)
      << label;
  auto from_structured = ce::http::from_message<C>(structured_request);
  expect(from_structured.has_value()) << label;
  if (from_structured) {
    expect(from_structured->id().view() == "1234-1234-1234"sv) << label;
    expect(from_structured->type().view() == "com.example.someevent"sv) << label;
    expect(from_structured->source().view() == "/mycontext/subcontext"sv) << label;
    expect(from_structured->extensions().size() == 1U) << label;
    // The structured document and the binary headers describe one event, so the
    // two modes must land on the same value apart from the extension the binary
    // example carries in addition.
    ce::event aligned = *from_binary;
    expect(aligned.remove_extension("exampleextension2")) << label;
    expect(bool{*from_structured == ce_test::as_decoded(aligned)})
        << label << ": the two content modes disagree about the same event";
  }

  // Batched mode, section 3.3: the same event twice in one message.
  const std::string batch_body =
      "[" + std::string{spec_structured_body} + "," + std::string{spec_structured_body} + "]";
  const ce::message batch_request{
      .header_fields = {{"content-type", "application/cloudevents-batch+json; charset=UTF-8"}},
      .body = ce::http::detail::to_bytes(batch_body),
  };
  expect(ce::http::detect_content_mode(batch_request) == ce::content_mode::batched) << label;
  auto from_batch = ce::http::from_batch_message<C>(batch_request);
  expect(from_batch.has_value()) << label;
  if (from_batch && from_structured) {
    expect(from_batch->size() == 2U) << label;
    for (const auto& element : *from_batch) {
      expect(bool{element == *from_structured}) << label << ": batch element differs";
    }
  }
}

// --- suites ------------------------------------------------------------------
//
// Each one runs the shared checks against both codecs.

template <class C>
void check_ce_header_name_grammar(std::string_view label) {
  using namespace boost::ut;

  for (const std::string_view name : {
           "ce-\tsubject"sv,
           "ce-has_underscore"sv,
           "ce-has-dash"sv,
           "ce-has space"sv,
           "ce-"sv,
       }) {
    auto decoded = ce::http::from_message<C>(binary_message_with({std::string{name}, "x"}));
    expect(!decoded.has_value()) << label << ": should reject header " << name;
    if (!decoded) {
      expect(decoded.error().code == ce::errc::invalid_attribute_name) << label << ": " << name;
    }
  }

  // A header that is present but empty is a second way to reach an event the
  // encoder would refuse; the presence check alone does not catch it.
  {
    const ce::message empty_type{.header_fields = {{"ce-specversion", "1.0"},
                                                   {"ce-id", "1"},
                                                   {"ce-source", "/s"},
                                                   {"ce-type", ""}}};
    expect(!ce::http::from_message<C>(empty_type).has_value()) << label << ": empty ce-type";
  }

  // A legal extension header still decodes, what decodes re-encodes, and the
  // re-encoded message decodes to the same event.
  auto decoded = ce::http::from_message<C>(binary_message_with({"ce-seq9", "x"}));
  expect(decoded.has_value()) << label;
  if (decoded) {
    const auto re_sent = ce::http::to_message<C>(*decoded, ce::content_mode::binary_mode);
    expect(re_sent.has_value()) << label;
    if (re_sent) {
      const auto again = ce::http::from_message<C>(*re_sent);
      expect(again.has_value() && bool{*again == *decoded}) << label;
    }
  }
}

// spec: SWR-HTTP-0001
const boost::ut::suite<"message-type-shape"> message_type_shape = [] {
  using namespace boost::ut;

  "message is an aggregate of headers and a binary body"_test = [] {
    // A plain aggregate, so a caller can build one with a designated initializer
    // and copy an arbitrary server's headers and body into it.
    static_assert(std::is_aggregate_v<ce::message>);
    static_assert(std::is_same_v<decltype(ce::message::header_fields), ce::raw_headers>);
    static_assert(std::is_same_v<decltype(ce::message::body), ce::binary>);
    static_assert(std::is_same_v<ce::binary, std::vector<std::byte>>);
    static_assert(std::is_default_constructible_v<ce::message>);
    static_assert(std::is_copy_constructible_v<ce::message>);
    static_assert(std::is_move_constructible_v<ce::message>);
    expect(true);
  };

  "a default message is empty and two of them compare equal"_test = [] {
    const ce::message left;
    const ce::message right;
    expect(left.header_fields.empty());
    expect(left.body.empty());
    expect(bool{left == right});

    const ce::message filled{.header_fields = {}, .body = ce::http::detail::to_bytes("x")};
    expect(bool{!(filled == left)});
  };

  "the binding names no HTTP library type"_test = [] {
    // The body is the core binary type and the headers are the SDK's own
    // container, so adapting any server is copying two members. That the header
    // DECLARES no third-party type is what the no-dependency build proves; it is
    // not something a runtime assertion can see, so it is asserted here only as
    // far as the two member types go.
    static_assert(std::is_same_v<ce::raw_headers::entry, std::pair<std::string, std::string>>);
    const ce::message request{.header_fields = {{"Content-Type", "text/plain"}},
                              .body = ce::http::detail::to_bytes("payload")};
    expect(request.header_fields.size() == 1U);
    expect(ce::http::detail::to_text(request.body) == "payload"sv);
  };
};

// spec: SWR-HTTP-0002
const boost::ut::suite<"headers-ordered-multimap"> headers_ordered_multimap = [] {
  using namespace boost::ut;

  "add preserves insertion order"_test = [] {
    ce::raw_headers fields;
    fields.add("A", "1");
    fields.add("B", "2");
    fields.add("C", "3");

    const std::pair<std::string_view, std::string_view> expected[] = {
        {"A"sv, "1"sv}, {"B"sv, "2"sv}, {"C"sv, "3"sv}};
    std::size_t index = 0;
    for (const auto& [name, value] : fields) {
      expect(index < std::size(expected));
      if (index >= std::size(expected)) {
        break;
      }
      expect(name == expected[index].first) << name;
      expect(value == expected[index].second) << value;
      ++index;
    }
    expect(index == 3U);
  };

  "add keeps repeated field names, in order"_test = [] {
    // The HTTP binding permits a repeated header, so a structure that merged
    // them would lose information the receiver may need.
    ce::raw_headers fields;
    fields.add("Set-Cookie", "a=1");
    fields.add("X-Other", "x");
    fields.add("Set-Cookie", "b=2");

    expect(fields.size() == 3U);
    std::vector<std::string> cookies;
    for (const auto& [name, value] : fields) {
      if (ce::detail::iequals(name, "set-cookie")) {
        cookies.push_back(value);
      }
    }
    expect(cookies.size() == 2U);
    if (cookies.size() == 2U) {
      expect(cookies[0] == "a=1"sv) << cookies[0];
      expect(cookies[1] == "b=2"sv) << cookies[1];
    }
    // A lookup answers with the first of them, which is the one a peer sent first.
    const auto* first = fields.find("Set-Cookie");
    expect(first != nullptr);
    if (first != nullptr) {
      expect(*first == "a=1"sv) << *first;
    }
  };

  "set replaces every header of that name"_test = [] {
    ce::raw_headers fields;
    fields.add("Accept", "text/plain");
    fields.add("X-Keep", "keep");
    fields.add("accept", "application/json");
    expect(fields.size() == 3U);

    // Not just the first: leaving a stale duplicate behind would mean a receiver
    // could still read the value the caller meant to replace.
    fields.set("ACCEPT", "application/xml");
    expect(fields.size() == 2U);
    std::size_t accept_count = 0;
    for (const auto& [name, value] : fields) {
      if (ce::detail::iequals(name, "accept")) {
        ++accept_count;
        expect(value == "application/xml"sv) << value;
      }
    }
    expect(accept_count == 1U);
    expect(fields.find("X-Keep") != nullptr);

    // set() on an absent name adds it, so it is usable without a contains() test.
    fields.set("New", "value");
    const auto* added = fields.find("new");
    expect(added != nullptr);
    if (added != nullptr) {
      expect(*added == "value"sv);
    }
  };

  "lookup ignores letter case, in both directions"_test = [] {
    ce::raw_headers fields;
    fields.add("Content-Type", "text/plain");
    fields.add("CE-SPECVERSION", "1.0");

    for (const auto spelling : {"Content-Type"sv, "content-type"sv, "CONTENT-TYPE"sv,
                                "cOnTeNt-TyPe"sv}) {
      const auto* found = fields.find(spelling);
      expect(found != nullptr) << spelling;
      expect(fields.contains(spelling)) << spelling;
      if (found != nullptr) {
        expect(*found == "text/plain"sv) << spelling;
      }
    }
    expect(fields.contains("ce-specversion"));
    expect(fields.find("ce-specversion") != nullptr);

    // A name that is not there is absent, so the match is not simply permissive.
    expect(fields.find("ce-id") == nullptr);
    expect(!fields.contains("ce-id"));
    expect(fields.find("Content-Type-Extra") == nullptr);
    expect(fields.find("ontent-Type") == nullptr);
  };

  "an empty container is empty"_test = [] {
    const ce::raw_headers fields;
    expect(fields.empty());
    expect(fields.size() == 0U);
    expect(fields.find("anything") == nullptr);
    expect(fields.begin() == fields.end());
  };
};

// spec: SWR-HTTP-0003
const boost::ut::suite<"to-message-modes"> to_message_modes = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_to_message_modes<C>(codec); };
  });

  "the send-side entry points have the declared signatures"_test = [] {
    static_assert(
        std::is_same_v<decltype(ce::http::to_message<nlohmann_codec>(
                           std::declval<const ce::event&>(), ce::content_mode::binary_mode)),
                       ce::result<ce::message>>);
    static_assert(
        std::is_same_v<decltype(ce::http::to_message<mini_codec>(
                           std::declval<const ce::event&>(), ce::content_mode::structured)),
                       ce::result<ce::message>>);
    expect(true);
  };
};

// spec: SWR-HTTP-0004
const boost::ut::suite<"from-message-roundtrip"> from_message_roundtrip = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_from_message_roundtrip<C>(codec); };
    test(std::string{codec} + " document payload") = [codec] {
      check_document_round_trip<C>(codec);
    };
  });
};

// spec: SWR-HTTP-0004
// spec: SWR-HTTP-0005
const boost::ut::suite<"structured-body-read-in-place"> structured_body_read_in_place = [] {
  using namespace boost::ut;

  "the body view covers every byte and nothing else"_test = [] {
    const ce::binary empty{};
    expect(ce::http::detail::text_of(empty).empty());

    const ce::binary bytes = ce::http::detail::to_bytes("a\xC5\xBCz");
    const std::string_view viewed = ce::http::detail::text_of(bytes);
    expect(viewed == "a\xC5\xBCz"sv);
    expect(static_cast<const void*>(viewed.data()) == static_cast<const void*>(bytes.data()));
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_structured_body_is_read_in_place<C>(codec); };
  });
};

// spec: SWR-HTTP-0005
const boost::ut::suite<"batched-variants"> batched_variants = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_batched_variants<C>(codec); };
  });

  "the batch entry points have the declared signatures"_test = [] {
    static_assert(std::is_same_v<decltype(ce::http::to_batch_message<nlohmann_codec>(
                                     std::declval<std::span<const ce::event>>())),
                                 ce::result<ce::message>>);
    static_assert(std::is_same_v<decltype(ce::http::from_batch_message<mini_codec>(
                                     std::declval<const ce::message&>())),
                                 ce::result<std::vector<ce::event>>>);
    expect(true);
  };
};

// spec: SWR-HTTP-0006
const boost::ut::suite<"content-mode-detection"> content_mode_detection = [] {
  using namespace boost::ut;

  "the batch prefix is tested before the cloudevents prefix"_test = [] {
    // application/cloudevents-batch+json ALSO starts with application/cloudevents,
    // so a detector that tested the shorter prefix first would report structured
    // and a batch would be decoded as a single event. The overlap is asserted
    // here explicitly, because every other case in this suite passes either way.
    expect(ce::json::batch_content_type.starts_with("application/cloudevents"sv))
        << "the premise of the ordering rule no longer holds";

    ce::message request;
    request.header_fields.set("Content-Type", std::string{ce::json::batch_content_type});
    expect(ce::http::detect_content_mode(request) == ce::content_mode::batched);
    expect(ce::http::detect_content_mode(request) != ce::content_mode::structured);
  };

  "no Content-Type at all is the binary mode"_test = [] {
    // Binary mode is what an ordinary HTTP request looks like, so it is also the
    // answer when the peer declared no media type at all.
    const ce::message bare;
    expect(ce::http::detect_content_mode(bare) == ce::content_mode::binary_mode);

    ce::message with_other_headers;
    with_other_headers.header_fields.set("ce-specversion", "1.0");
    with_other_headers.header_fields.set("Accept", "*/*");
    expect(ce::http::detect_content_mode(with_other_headers) == ce::content_mode::binary_mode);
  };

  "each media type selects its mode"_test = [] {
    const std::pair<std::string_view, ce::content_mode> cases[] = {
        {"application/cloudevents-batch+json"sv, ce::content_mode::batched},
        {"application/cloudevents-batch+json; charset=utf-8"sv, ce::content_mode::batched},
        {"APPLICATION/CLOUDEVENTS-BATCH+JSON"sv, ce::content_mode::batched},
        {"application/cloudevents+json"sv, ce::content_mode::structured},
        // A parameterised media type stays in the structured branch, which is
        // why the test is a prefix test and not an equality test.
        {"application/cloudevents+json; charset=utf-8"sv, ce::content_mode::structured},
        {"Application/CloudEvents+JSON"sv, ce::content_mode::structured},
        {"application/cloudevents+xml"sv, ce::content_mode::structured},
        {"application/json"sv, ce::content_mode::binary_mode},
        {"text/plain"sv, ce::content_mode::binary_mode},
        {"application/octet-stream"sv, ce::content_mode::binary_mode},
        {""sv, ce::content_mode::binary_mode},
        // Near misses: the prefix has to be a prefix.
        {"x-application/cloudevents+json"sv, ce::content_mode::binary_mode},
        {"application/cloudevent+json"sv, ce::content_mode::binary_mode},
    };
    for (const auto& [media_type, mode] : cases) {
      ce::message request;
      request.header_fields.set("Content-Type", std::string{media_type});
      expect(ce::http::detect_content_mode(request) == mode) << media_type;
    }
  };

  "the Content-Type header name is matched case-insensitively"_test = [] {
    for (const auto spelling : {"Content-Type"sv, "content-type"sv, "CONTENT-TYPE"sv}) {
      ce::message request;
      request.header_fields.set(std::string{spelling}, std::string{ce::json::content_type});
      expect(ce::http::detect_content_mode(request) == ce::content_mode::structured) << spelling;
    }
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_detection_of_produced_messages<C>(codec); };
  });
};

// spec: SWR-HTTP-0007
const boost::ut::suite<"binary-mode-ce-headers"> binary_mode_ce_headers = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_binary_mode_ce_headers<C>(codec); };
  });
};

// spec: SWR-HTTP-0008
const boost::ut::suite<"datacontenttype-header-mapping"> datacontenttype_header_mapping = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_datacontenttype_mapping<C>(codec); };
  });
};

// spec: SWR-HTTP-0009
const boost::ut::suite<"binary-mode-body-is-raw-data"> binary_mode_body_is_raw_data = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_binary_mode_body<C>(codec); };
  });
};

// spec: SWR-HTTP-0010
const boost::ut::suite<"header-value-percent-encoding"> header_value_percent_encoding = [] {
  using namespace boost::ut;

  "the three specials and every non-ASCII octet are escaped"_test = [] {
    // Directly on the encoder, so the rule is pinned independently of which
    // attribute happens to carry the value.
    expect(ce::http::detail::percent_encode(" ") == "%20"sv);
    expect(ce::http::detail::percent_encode("\"") == "%22"sv);
    expect(ce::http::detail::percent_encode("%") == "%25"sv);
    expect(ce::http::detail::percent_encode(std::string{two_byte_utf8}) == "%C3%BC"sv);
    expect(ce::http::detail::percent_encode(std::string{three_byte_utf8}) == "%E6%97%A5"sv);
    expect(ce::http::detail::percent_encode(std::string{four_byte_utf8}) == "%F0%9F%98%80"sv);
    // Control characters are outside printable ASCII and go the same way.
    expect(ce::http::detail::percent_encode("\n") == "%0A"sv);
    expect(ce::http::detail::percent_encode("\t") == "%09"sv);
    expect(ce::http::detail::percent_encode(std::string{'\x7F'}) == "%7F"sv);
    // The hex digits are upper case, so two senders produce the same bytes.
    expect(ce::http::detail::percent_encode(std::string{'\xAB'}) == "%AB"sv);

    // Printable ASCII other than the quote and the percent is left alone.
    constexpr auto untouched = "!#$&'()*+,-./0123456789:;<=>?@ABCXYZ[\\]^_`abcxyz{|}~"sv;
    expect(ce::http::detail::percent_encode(untouched) == untouched);

    // needs_escape is the single rule both of those follow.
    static_assert(ce::http::detail::needs_escape(' '));
    static_assert(ce::http::detail::needs_escape('"'));
    static_assert(ce::http::detail::needs_escape('%'));
    static_assert(ce::http::detail::needs_escape(0x20));
    static_assert(ce::http::detail::needs_escape(0x7F));
    static_assert(ce::http::detail::needs_escape(0x80));
    static_assert(!ce::http::detail::needs_escape('!'));
    static_assert(!ce::http::detail::needs_escape('~'));
    static_assert(!ce::http::detail::needs_escape('A'));
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_percent_encoding_roundtrip<C>(codec); };
  });
};

// spec: SWR-HTTP-0011
const boost::ut::suite<"header-value-percent-decoding"> header_value_percent_decoding = [] {
  using namespace boost::ut;

  "any octet may arrive escaped, including ones that needed no escape"_test = [] {
    // Another SDK is free to escape more than the minimum, so %41 must decode to
    // A even though this SDK would never have written it that way.
    const std::pair<std::string_view, std::string_view> cases[] = {
        {"%41"sv, "A"sv},   {"%7E"sv, "~"sv},  {"%2F"sv, "/"sv},
        {"%3A"sv, ":"sv},   {"%25"sv, "%"sv},  {"%20"sv, " "sv},
        {"%22"sv, "\""sv},  {"%61%62"sv, "ab"sv},
    };
    for (const auto& [encoded, decoded] : cases) {
      auto result = ce::http::detail::percent_decode(encoded);
      expect(result.has_value()) << encoded;
      if (result) {
        expect(*result == decoded) << encoded << " gave " << *result;
      }
    }

    // Lower-case hex digits are accepted on the receive path even though the
    // encoder writes upper case.
    auto lower = ce::http::detail::percent_decode("%c3%bc");
    expect(lower.has_value());
    if (lower) {
      expect(*lower == two_byte_utf8);
    }
  };

  "a truncated or non-hexadecimal escape is a parse error"_test = [] {
    for (const auto broken : {"%"sv, "%4"sv, "abc%4"sv, "abc%"sv, "%ZZ"sv, "%4Z"sv, "%Z4"sv,
                              "%G0"sv, "a%-1b"sv}) {
      auto refused = ce::http::detail::percent_decode(broken);
      expect(!refused.has_value()) << "should reject " << broken;
      if (!refused) {
        expect(refused.error().code == ce::errc::parse_error)
            << broken << " gave " << ce::to_string_view(refused.error().code);
      }
    }
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_percent_decoding<C>(codec); };
  });
};

// spec: SWR-HTTP-0012
const boost::ut::suite<"header-value-invalid-utf8"> header_value_invalid_utf8 = [] {
  using namespace boost::ut;

  "ill-formed UTF-8 is rejected in every one of its forms"_test = [] {
    // Each of these is a way to spell something a consumer comparing strings
    // would read differently from its well-formed twin.
    expect(ce::http::detail::is_valid_utf8("plain ascii"));
    expect(ce::http::detail::is_valid_utf8(two_byte_utf8));
    expect(ce::http::detail::is_valid_utf8(three_byte_utf8));
    expect(ce::http::detail::is_valid_utf8(four_byte_utf8));
    expect(ce::http::detail::is_valid_utf8(""sv));

    expect(!ce::http::detail::is_valid_utf8("\xC0\x80"sv));          // overlong NUL
    expect(!ce::http::detail::is_valid_utf8("\xC1\xBF"sv));          // overlong
    expect(!ce::http::detail::is_valid_utf8("\xE0\x80\xAF"sv));      // overlong solidus
    expect(!ce::http::detail::is_valid_utf8("\xF0\x80\x80\xAF"sv));  // overlong, four bytes
    expect(!ce::http::detail::is_valid_utf8("\xED\xA0\x80"sv));      // surrogate D800
    expect(!ce::http::detail::is_valid_utf8("\xED\xBF\xBF"sv));      // surrogate DFFF
    expect(!ce::http::detail::is_valid_utf8("\xF5\x80\x80\x80"sv));  // above U+10FFFF
    expect(!ce::http::detail::is_valid_utf8("\xF4\x90\x80\x80"sv));  // above U+10FFFF
    expect(!ce::http::detail::is_valid_utf8("\xE2\x82"sv));          // truncated
    expect(!ce::http::detail::is_valid_utf8("\x80"sv));              // stray continuation
    expect(!ce::http::detail::is_valid_utf8("\xFE"sv));              // never a lead byte
    expect(!ce::http::detail::is_valid_utf8("\xFF"sv));

    // constexpr, so a caller may use it where no runtime check would run.
    static_assert(ce::http::detail::is_valid_utf8("ascii"));
    static_assert(!ce::http::detail::is_valid_utf8("\xC0\x80"));
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_invalid_utf8_rejected<C>(codec); };
  });
};

// spec: SWR-HTTP-0013
const boost::ut::suite<"binary-mode-extension-string-type"> binary_mode_extension_string_type = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_extension_string_type<C>(codec); };
  });
};

// spec: SWR-HTTP-0014
const boost::ut::suite<"not-a-cloudevent-distinct-from-malformed">
    not_a_cloudevent_distinct_from_malformed = [] {
      using namespace boost::ut;

      ce_test::for_each_codec([]<class C>(std::string_view codec) {
        test(std::string{codec}) = [codec] { check_not_a_cloudevent<C>(codec); };
      });
    };

// The three content modes of the system requirement are exercised end to end on
// the binding specification's own example, with no socket anywhere: the entry
// points take and return plain values, so the SDK performs no network I/O.
// spec: SYS-HTTP-0001
const boost::ut::suite<"http-binding-spec-examples"> http_binding_spec_examples = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_spec_examples<C>(codec); };
  });
};

// spec: SWR-HTTP-0015
const boost::ut::suite<"decoded-message-always-validates"> ce_header_name_grammar = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_ce_header_name_grammar<C>(codec); };
  });
};

// spec: SWR-MSG-0001
// spec: SYS-MSG-0001
const boost::ut::suite<"headers-case-sensitive-lookup"> headers_exact = [] {
  using namespace boost::ut;

  "find_exact answers only to the exact spelling"_test = [] {
    ce::raw_headers fields;
    fields.add("ce-id", "lower");
    fields.add("CE-ID", "upper");

    // The case-insensitive form answers with the first of the two.
    const auto* lenient = fields.find("Ce-Id");
    expect(lenient != nullptr);
    if (lenient != nullptr) {
      expect(*lenient == "lower");
    }

    const auto* exact_lower = fields.find_exact("ce-id");
    const auto* exact_upper = fields.find_exact("CE-ID");
    expect(exact_lower != nullptr);
    expect(exact_upper != nullptr);
    if (exact_lower != nullptr && exact_upper != nullptr) {
      expect(*exact_lower == "lower");
      expect(*exact_upper == "upper");
    }
    // A spelling neither header uses matches nothing, where find would match.
    expect(fields.find_exact("Ce-Id") == nullptr);
    expect(fields.find("Ce-Id") != nullptr);
  };

  "contains_exact is the same rule"_test = [] {
    ce::raw_headers fields;
    fields.add("ce_partitionkey", "k");
    expect(fields.contains_exact("ce_partitionkey"));
    expect(!fields.contains_exact("CE_PARTITIONKEY"));
    // The lenient form does not distinguish them.
    expect(fields.contains("CE_PARTITIONKEY"));
  };

  "set_exact replaces only the exact spelling"_test = [] {
    // This is the reason set_exact exists. A Kafka binding writing extension
    // "abc" must not erase a caller's "ABC", which is a different header on
    // that transport.
    ce::raw_headers fields;
    fields.add("abc", "one");
    fields.add("ABC", "two");

    fields.set_exact("abc", "replaced");
    expect(fields.size() == 2_ul) << "set_exact removed the other spelling";

    const auto* lower = fields.find_exact("abc");
    const auto* upper = fields.find_exact("ABC");
    expect(lower != nullptr);
    expect(upper != nullptr);
    if (lower != nullptr && upper != nullptr) {
      expect(*lower == "replaced");
      expect(*upper == "two") << "the other spelling was disturbed";
    }
  };

  "set still erases case-insensitively, as HTTP needs"_test = [] {
    // SWR-HTTP-0002 is unchanged: the existing three keep their behaviour, and
    // that is what stops this addition being a breaking change.
    ce::raw_headers fields;
    fields.add("abc", "one");
    fields.add("ABC", "two");

    fields.set("abc", "replaced");
    expect(fields.size() == 1_ul) << "set stopped erasing case-insensitively";

    const auto* only = fields.find("ABC");
    expect(only != nullptr);
    if (only != nullptr) {
      expect(*only == "replaced");
    }
  };

  "set_exact adds when absent"_test = [] {
    ce::raw_headers fields;
    fields.set_exact("ce_type", "t");
    expect(fields.size() == 1_ul);
    expect(fields.contains_exact("ce_type"));
  };
};


// The wire shapes below are not invented: they were produced by running
// sdk-go v2.15.2 on 2026-09-21. Go writes `ce-subject: a b` for subject "a b"
// and `ce-pct: 100%` for a literal percent, and reads a percent-encoded value
// back without decoding it.
template <class Codec>
void check_value_policy(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event({.subject = "a b"_subject});

  // The default is unchanged and stays what the binding specification requires.
  auto conformant = ce::http::to_message<Codec>(subject, ce::content_mode::binary_mode);
  expect(bool{conformant}) << codec;
  if (conformant) {
    expect(*conformant->header_fields.find("ce-subject") == "a%20b"sv) << codec;
  }

  // The opt-in policy writes what Go and Java write.
  auto literal =
      ce::http::to_message<Codec, ce::http::literal_values>(subject, ce::content_mode::binary_mode);
  expect(bool{literal}) << codec;
  if (literal) {
    expect(*literal->header_fields.find("ce-subject") == "a b"sv) << codec;
  }

  // A literal percent is what a Go sender puts on the wire. The conformant
  // reader refuses it as a truncated escape; the literal reader takes it.
  const ce::message from_go = binary_message_with({"ce-subject", "100%"});

  auto strict = ce::http::from_message<Codec>(from_go);
  expect(!strict) << codec;

  auto lenient = ce::http::from_message<Codec, ce::http::literal_values>(from_go);
  expect(bool{lenient}) << codec;
  if (lenient) {
    expect(lenient->subject().has_value() && lenient->subject()->view() == "100%"sv) << codec;
  }

  // And the escape a conformant sender produced is NOT decoded under the
  // literal policy, which is exactly how Go misreads us. Naming the policy is
  // choosing that behaviour.
  auto as_go_sees_it = ce::http::from_message<Codec, ce::http::literal_values>(
      binary_message_with({"ce-subject", "a%20b"}));
  expect(bool{as_go_sees_it}) << codec;
  if (as_go_sees_it) {
    expect(as_go_sees_it->subject().has_value() &&
           as_go_sees_it->subject()->view() == "a%20b"sv)
        << codec;
  }
}

// Percent-encoding was also what kept a line break out of a header field, so
// the literal policy has to refuse one itself.
template <class Codec>
void check_literal_refuses_control_characters(std::string_view codec) {
  using namespace boost::ut;

  // std::string, not const char*: a char pointer stops at the embedded NUL, so
  // the last case would reach the binding as "a" and prove nothing.
  const std::string injected_values[] = {"a\rb", "a\nb", "a\r\nX-Evil: 1",
                                         std::string{"a\0b", 3}};
  for (const std::string& injected : injected_values) {
    // Well-formed UTF-8, so `subject` holds it: refusing a line break is the
    // value policy's rule, not the attribute's.
    const auto made = ce::subject::make(injected);
    expect(made.has_value()) << codec;
    if (!made) {
      continue;
    }
    const ce::event subject = base_event({.subject = *made});
    auto written = ce::http::to_message<Codec, ce::http::literal_values>(
        subject, ce::content_mode::binary_mode);
    expect(!written) << codec << " accepted a control character";
    if (!written) {
      expect(written.error().code == ce::errc::invalid_argument) << codec;
    }
  }

  // The conformant policy escapes them instead, which is equally safe.
  const ce::event subject = base_event({.subject = "a\r\nX-Evil: 1"_subject});
  auto escaped = ce::http::to_message<Codec>(subject, ce::content_mode::binary_mode);
  expect(bool{escaped}) << codec;
  if (escaped) {
    expect(escaped->header_fields.find("ce-subject")->find('\r') == std::string::npos) << codec;
    expect(escaped->header_fields.find("ce-subject")->find('\n') == std::string::npos) << codec;
  }
}

}  // namespace

// spec: SWR-HTTP-0016
const boost::ut::suite<"http-value-policy"> http_value_policy = [] {
  using namespace boost::ut;

  static_assert(ce::http::value_policy<ce::http::percent_encoded_values>);
  static_assert(ce::http::value_policy<ce::http::literal_values>);

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      check_value_policy<C>(codec);
      check_literal_refuses_control_characters<C>(codec);
    };
  });
};

int main() {}
