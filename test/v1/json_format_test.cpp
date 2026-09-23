#include <boost/ut.hpp>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/v1/core.hpp>
#include <cloudevents/v1/detail/timestamp.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/v1/format/json_format.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "codecs_under_test.hpp"
#include "json_format_checks.hpp"
#include "mini_codec.hpp"

// json_format owns every CloudEvents JSON rule, over whatever codec it is given.
// So every assertion below is written once, as a function template over the
// codec, and every suite instantiates it for BOTH in-tree codecs. A suite that
// exercised only nlohmann would be testing nlohmann rather than the abstraction,
// and mini_codec would exist for nothing.

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::v1::codec::nlohmann_codec;
using mini_codec = ce::v1::test::mini_codec;

/// A minimal event that passes validate(), built with ONE designated initializer.
[[nodiscard]] auto base_event() -> ce::v1::event {
  return ce::v1::event{
      .id = "1",
      .source = "/spec/test",
      .type = "com.example.thing",
  };
}

/// Encode `subject` and parse the result back into the codec's own DOM, so a
/// suite can assert the JSON shape without depending on how a codec spells it.
template <class C>
[[nodiscard]] auto encoded_document(const ce::v1::event& subject) -> ce::v1::result<typename C::value> {
  auto text = ce::v1::json_format<C>::encode(subject);
  if (!text) {
    return ce::v1::fail(text.error().code, text.error().detail, text.error().where);
  }
  return C::parse(*text);
}

// --- SWR-JSON-0010: the four entry points ----------------------------------

template <class C>
void check_entry_points(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  ce::v1::event subject = base_event();
  subject.datacontenttype = "application/json";
  subject.subject = "s";
  subject.dataschema = ce::v1::uri{"https://example.com/schema"};
  const auto parsed_time = ce::v1::parse_timestamp("2018-04-05T17:31:00Z");
  expect(parsed_time.has_value()) << label;
  if (!parsed_time) {
    return;
  }
  subject.time = *parsed_time;
  expect(subject.set_extension("seq", std::int32_t{7}).has_value()) << label;
  expect(subject.set_extension("flag", true).has_value()) << label;
  expect(subject.set_extension("tag", std::string{"x"}).has_value()) << label;
  subject.data = ce::v1::json_text{.raw = R"({"k":1})"};

  auto text = format::encode(subject);
  expect(text.has_value()) << label << ": encode";
  if (!text) {
    return;
  }

  auto back = format::decode(*text);
  expect(back.has_value()) << label << ": decode";
  if (!back) {
    expect(false) << label << ": " << back.error().detail;
    return;
  }
  expect(back->id == subject.id) << label;
  expect(back->type == subject.type) << label;
  expect(bool{back->source == subject.source}) << label;
  expect(back->specversion == "1.0") << label;
  expect(back->subject == subject.subject) << label;
  expect(back->datacontenttype == subject.datacontenttype) << label;
  expect(bool{back->dataschema == subject.dataschema}) << label;
  expect(back->time.has_value()) << label;
  if (back->time) {
    expect(ce::v1::to_string(*back->time) == "2018-04-05T17:31:00Z") << label;
  }
  expect(back->extensions.size() == 3U) << label;

  const std::vector<ce::v1::event> many{subject, base_event()};
  auto batch_text = format::encode_batch(std::span<const ce::v1::event>{many});
  expect(batch_text.has_value()) << label << ": encode_batch";
  if (!batch_text) {
    return;
  }
  auto batch_back = format::decode_batch(*batch_text);
  expect(batch_back.has_value()) << label << ": decode_batch";
  if (batch_back) {
    expect(batch_back->size() == 2U) << label;
  }

  // Both decoding operations report failure as a result rather than out of band.
  auto malformed = format::decode("{");
  expect(!malformed.has_value()) << label;
  auto malformed_batch = format::decode_batch("[");
  expect(!malformed_batch.has_value()) << label;
  // A batch document that is not an array is rejected as such.
  auto not_an_array = format::decode_batch("{}");
  expect(!not_an_array.has_value()) << label;
  // An invalid event never encodes.
  ce::v1::event invalid = base_event();
  invalid.id.clear();
  expect(!format::encode(invalid).has_value()) << label;
}

// --- SWR-JSON-0011: Integer attributes as JSON numbers ---------------------

template <class C>
void check_integer_attribute(std::string_view label) {
  using namespace boost::ut;

  constexpr auto lowest = std::numeric_limits<std::int32_t>::min();
  constexpr auto highest = std::numeric_limits<std::int32_t>::max();

  ce::v1::event subject = base_event();
  expect(subject.set_extension("seq", std::int32_t{7}).has_value()) << label;
  expect(subject.set_extension("low", lowest).has_value()) << label;
  expect(subject.set_extension("high", highest).has_value()) << label;

  auto document = encoded_document<C>(subject);
  expect(document.has_value()) << label;
  if (!document) {
    return;
  }

  const std::pair<std::string_view, std::int64_t> expected[] = {
      {"seq"sv, 7}, {"low"sv, lowest}, {"high"sv, highest}};
  for (const auto& [name, value] : expected) {
    const auto* member = C::find(*document, name);
    expect(member != nullptr) << label << ": " << name;
    if (member == nullptr) {
      continue;
    }
    // A JSON number, not a JSON string: a peer reading the wire must see a
    // number or the CloudEvents Integer type has not been expressed.
    expect(C::kind_of(*member) == ce::v1::json::kind::integer) << label << ": " << name;
    const auto held = C::as_int(*member);
    expect(held.has_value()) << label << ": " << name;
    if (held) {
      expect(*held == value) << label << ": " << name;
    }
  }

  // The round trip keeps it an Integer rather than promoting it.
  auto text = ce::v1::json_format<C>::encode(subject);
  expect(text.has_value()) << label;
  if (!text) {
    return;
  }
  auto back = ce::v1::json_format<C>::decode(*text);
  expect(back.has_value()) << label;
  if (!back) {
    return;
  }
  const auto* seq = back->extension("seq");
  expect(seq != nullptr) << label;
  if (seq != nullptr) {
    expect(std::holds_alternative<std::int32_t>(*seq)) << label;
    expect(std::get<std::int32_t>(*seq) == 7) << label;
  }
  const auto* high = back->extension("high");
  if (high != nullptr && std::holds_alternative<std::int32_t>(*high)) {
    expect(std::get<std::int32_t>(*high) == highest) << label;
  }
}

// --- SWR-JSON-0012: out-of-range or fractional Integer ---------------------

template <class C>
void check_integer_rejections(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  constexpr auto prefix = R"({"specversion":"1.0","id":"1","source":"/s","type":"t",)"sv;

  const std::pair<std::string_view, ce::v1::errc> cases[] = {
      // Above int32 max, below int32 min: truncating either would hand the
      // caller an event the peer never sent.
      {R"("big":9999999999})"sv, ce::v1::errc::out_of_range},
      {R"("small":-9999999999})"sv, ce::v1::errc::out_of_range},
      {R"("edge":2147483648})"sv, ce::v1::errc::out_of_range},
      {R"("edge":-2147483649})"sv, ce::v1::errc::out_of_range},
      // A fractional number is not an Integer at all.
      {R"("ratio":1.5})"sv, ce::v1::errc::type_mismatch},
      {R"("ratio":-0.25})"sv, ce::v1::errc::type_mismatch},
  };

  for (const auto& [tail, code] : cases) {
    const std::string document = std::string{prefix} + std::string{tail};
    auto decoded = format::decode(document);
    expect(!decoded.has_value()) << label << ": should reject " << document;
    if (!decoded) {
      expect(decoded.error().code == code) << label << ": " << document << " gave "
                                           << ce::v1::to_string_view(decoded.error().code);
    }
  }

  // The boundary values themselves are accepted, so the rejections above are
  // about the range and not about large numbers generally.
  for (const auto tail : {R"("edge":2147483647})"sv, R"("edge":-2147483648})"sv}) {
    const std::string document = std::string{prefix} + std::string{tail};
    auto decoded = format::decode(document);
    expect(decoded.has_value()) << label << ": should accept " << document;
  }
}

// --- SWR-JSON-0013: Binary attributes as base64 strings --------------------

template <class C>
void check_binary_attribute(std::string_view label) {
  using namespace boost::ut;

  const ce::v1::binary octets{std::byte{0x00}, std::byte{0x01}, std::byte{0xFF}, std::byte{0x10}};
  ce::v1::event subject = base_event();
  expect(subject.set_extension("blob", octets).has_value()) << label;

  auto document = encoded_document<C>(subject);
  expect(document.has_value()) << label;
  if (!document) {
    return;
  }
  const auto* member = C::find(*document, "blob");
  expect(member != nullptr) << label;
  if (member == nullptr) {
    return;
  }
  expect(C::kind_of(*member) == ce::v1::json::kind::string) << label;
  const auto held = C::as_string(*member);
  expect(held.has_value()) << label;
  if (!held) {
    return;
  }
  expect(*held == ce::v1::base64_encode(octets)) << label << ": " << *held;

  // The octets are recoverable from the string on the wire. The attribute comes
  // back as a std::string because JSON does not carry the CloudEvents type
  // (SWR-JSON-0023); what matters here is that no octet was lost.
  const auto recovered = ce::v1::base64_decode(*held);
  expect(recovered.has_value()) << label;
  if (recovered) {
    expect(*recovered == octets) << label;
  }
}

// --- SWR-JSON-0014: URI, URI-Reference and Timestamp as strings ------------

template <class C>
void check_textual_attributes(std::string_view label) {
  using namespace boost::ut;

  const auto parsed_time = ce::v1::parse_timestamp("2018-04-05T17:31:00Z");
  expect(parsed_time.has_value()) << label;
  if (!parsed_time) {
    return;
  }

  ce::v1::event subject = base_event();
  subject.dataschema = ce::v1::uri{"https://example.com/schema"};
  subject.time = *parsed_time;
  expect(subject.set_extension("schemaurl", ce::v1::uri{"https://example.com/x"}).has_value()) << label;
  expect(subject.set_extension("relref", ce::v1::uri_ref{"/relative/path"}).has_value()) << label;
  expect(subject.set_extension("seen", *parsed_time).has_value()) << label;

  auto document = encoded_document<C>(subject);
  expect(document.has_value()) << label;
  if (!document) {
    return;
  }

  const std::pair<std::string_view, std::string_view> expected[] = {
      {"source"sv, "/spec/test"sv},
      {"dataschema"sv, "https://example.com/schema"sv},
      {"time"sv, "2018-04-05T17:31:00Z"sv},
      {"schemaurl"sv, "https://example.com/x"sv},
      {"relref"sv, "/relative/path"sv},
      {"seen"sv, "2018-04-05T17:31:00Z"sv},
  };
  for (const auto& [name, rendered] : expected) {
    const auto* member = C::find(*document, name);
    expect(member != nullptr) << label << ": " << name;
    if (member == nullptr) {
      continue;
    }
    expect(C::kind_of(*member) == ce::v1::json::kind::string) << label << ": " << name;
    const auto held = C::as_string(*member);
    expect(held.has_value()) << label << ": " << name;
    if (held) {
      expect(*held == rendered) << label << ": " << name << " is " << *held;
    }
  }

  // The timestamp survives the round trip in its canonical RFC 3339 form.
  auto text = ce::v1::json_format<C>::encode(subject);
  expect(text.has_value()) << label;
  if (!text) {
    return;
  }
  auto back = ce::v1::json_format<C>::decode(*text);
  expect(back.has_value()) << label;
  if (back && back->time) {
    expect(ce::v1::to_string(*back->time) == "2018-04-05T17:31:00Z") << label;
  }
  if (back) {
    expect(bool{back->dataschema == subject.dataschema}) << label;
    expect(bool{back->source == subject.source}) << label;
  }
}

// --- SWR-JSON-0015 / 0016 / 0017: the three payload shapes on encode -------

template <class C>
void check_json_text_data(std::string_view label) {
  using namespace boost::ut;

  ce::v1::event subject = base_event();
  subject.datacontenttype = "application/json";
  subject.data = ce::v1::json_text{.raw = R"({"k":1,"list":[1,2]})"};

  auto document = encoded_document<C>(subject);
  expect(document.has_value()) << label;
  if (!document) {
    return;
  }

  const auto* data = C::find(*document, "data");
  expect(data != nullptr) << label << ": data must be present";
  expect(C::find(*document, "data_base64") == nullptr) << label;
  if (data == nullptr) {
    return;
  }

  // Spliced as parsed JSON, not escaped into a string: a consumer parses the
  // event once and has the payload.
  expect(C::kind_of(*data) == ce::v1::json::kind::object) << label;
  const auto* inner = C::find(*data, "k");
  expect(inner != nullptr) << label;
  if (inner != nullptr) {
    const auto held = C::as_int(*inner);
    expect(held.has_value()) << label;
    if (held) {
      expect(*held == 1) << label;
    }
  }
  const auto* list = C::find(*data, "list");
  expect(list != nullptr) << label;
  if (list != nullptr) {
    expect(C::kind_of(*list) == ce::v1::json::kind::array) << label;
    expect(C::size_of(*list) == 2U) << label;
  }

  // Malformed json_text is the one thing the SDK validates about a payload.
  ce::v1::event broken = base_event();
  broken.data = ce::v1::json_text{.raw = "{not json"};
  auto refused = ce::v1::json_format<C>::encode(broken);
  expect(!refused.has_value()) << label;
  if (!refused) {
    expect(refused.error().code == ce::v1::errc::parse_error) << label;
  }
}

template <class C>
void check_binary_data(std::string_view label) {
  using namespace boost::ut;

  const ce::v1::binary octets{std::byte{1}, std::byte{2}, std::byte{0xFF}};
  ce::v1::event subject = base_event();
  subject.data = octets;

  auto document = encoded_document<C>(subject);
  expect(document.has_value()) << label;
  if (!document) {
    return;
  }

  expect(C::find(*document, "data") == nullptr) << label << ": data must be absent";
  const auto* encoded = C::find(*document, "data_base64");
  expect(encoded != nullptr) << label << ": data_base64 must be present";
  if (encoded == nullptr) {
    return;
  }
  expect(C::kind_of(*encoded) == ce::v1::json::kind::string) << label;
  const auto held = C::as_string(*encoded);
  expect(held.has_value()) << label;
  if (held) {
    expect(*held == ce::v1::base64_encode(octets)) << label << ": " << *held;
  }

  // An empty payload is still binary and still uses data_base64.
  ce::v1::event empty_payload = base_event();
  empty_payload.data = ce::v1::binary{};
  auto empty_document = encoded_document<C>(empty_payload);
  expect(empty_document.has_value()) << label;
  if (empty_document) {
    expect(C::find(*empty_document, "data_base64") != nullptr) << label;
  }

  // No payload at all writes neither member.
  auto bare = encoded_document<C>(base_event());
  expect(bare.has_value()) << label;
  if (bare) {
    expect(C::find(*bare, "data") == nullptr) << label;
    expect(C::find(*bare, "data_base64") == nullptr) << label;
  }
}

template <class C>
void check_string_data(std::string_view label) {
  using namespace boost::ut;

  ce::v1::event subject = base_event();
  subject.datacontenttype = "text/plain";
  subject.data = std::string{"hello \"world\""};

  auto document = encoded_document<C>(subject);
  expect(document.has_value()) << label;
  if (!document) {
    return;
  }

  const auto* data = C::find(*document, "data");
  expect(data != nullptr) << label;
  expect(C::find(*document, "data_base64") == nullptr) << label;
  if (data == nullptr) {
    return;
  }
  expect(C::kind_of(*data) == ce::v1::json::kind::string) << label;
  const auto held = C::as_string(*data);
  expect(held.has_value()) << label;
  if (held) {
    // Carried verbatim, with the quoting handled by the codec rather than by
    // the format layer escaping it by hand.
    expect(*held == R"(hello "world")"sv) << label << ": " << *held;
  }
}

// --- SWR-JSON-0018 / 0019 / 0020: the payload shapes on decode -------------

template <class C>
void check_decode_data_base64(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  const ce::v1::binary octets{std::byte{1}, std::byte{2}, std::byte{0xFF}};
  const std::string document =
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":")" +
      ce::v1::base64_encode(octets) + R"("})";

  auto decoded = format::decode(document);
  expect(decoded.has_value()) << label;
  if (!decoded) {
    return;
  }
  expect(std::holds_alternative<ce::v1::binary>(decoded->data)) << label;
  if (std::holds_alternative<ce::v1::binary>(decoded->data)) {
    expect(std::get<ce::v1::binary>(decoded->data) == octets) << label;
  }

  // Unpadded base64 from a peer decodes to the same octets.
  auto unpadded_source = ce::v1::base64_encode(ce::v1::binary{std::byte{0xAB}});
  while (!unpadded_source.empty() && unpadded_source.back() == '=') {
    unpadded_source.pop_back();
  }
  const std::string unpadded =
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":")" +
      unpadded_source + R"("})";
  auto from_unpadded = format::decode(unpadded);
  expect(from_unpadded.has_value()) << label;
  if (from_unpadded && std::holds_alternative<ce::v1::binary>(from_unpadded->data)) {
    expect(std::get<ce::v1::binary>(from_unpadded->data) == ce::v1::binary{std::byte{0xAB}}) << label;
  }

  // Invalid base64 arriving from a peer is a typed error, not a partial payload.
  auto bad = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":"not!base64"})");
  expect(!bad.has_value()) << label;
  if (!bad) {
    expect(bad.error().code == ce::v1::errc::invalid_base64) << label;
  }

  // A non-string data_base64 is rejected rather than coerced.
  auto wrong_kind = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":5})");
  expect(!wrong_kind.has_value()) << label;
}

template <class C>
void check_decode_data_as_json_text(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  // No datacontenttype at all: json_text is the default.
  auto decoded = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data":{"k":1}})");
  expect(decoded.has_value()) << label;
  if (decoded) {
    expect(std::holds_alternative<ce::v1::json_text>(decoded->data)) << label;
    if (std::holds_alternative<ce::v1::json_text>(decoded->data)) {
      // The raw text must itself be well-formed JSON carrying the same value.
      const auto& raw = std::get<ce::v1::json_text>(decoded->data).raw;
      auto reparsed = C::parse(raw);
      expect(reparsed.has_value()) << label << ": " << raw;
      if (reparsed) {
        const auto* inner = C::find(*reparsed, "k");
        expect(inner != nullptr) << label;
        if (inner != nullptr) {
          const auto held = C::as_int(*inner);
          expect(held.has_value()) << label;
          if (held) {
            expect(*held == 1) << label;
          }
        }
      }
    }
  }

  // A JSON content type keeps a JSON string payload as json_text rather than
  // unwrapping it, because the producer said the payload is JSON.
  const std::pair<std::string_view, std::string_view> json_typed[] = {
      {R"("application/json")"sv, R"([1,2])"sv},
      {R"("application/vnd.example+json")"sv, R"({"a":true})"sv},
      {R"("application/json")"sv, R"("a string")"sv},
  };
  for (const auto& [content_type, payload] : json_typed) {
    const std::string document =
        std::string{R"({"specversion":"1.0","id":"1","source":"/s","type":"t","datacontenttype":)"} +
        std::string{content_type} + R"(,"data":)" + std::string{payload} + "}";
    auto typed = format::decode(document);
    expect(typed.has_value()) << label << ": " << document;
    if (typed) {
      expect(std::holds_alternative<ce::v1::json_text>(typed->data)) << label << ": " << document;
    }
  }

  // A non-string payload under a non-JSON content type is still json_text: the
  // string rule is about a JSON string, not about the content type alone.
  auto numeric = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","datacontenttype":"text/plain","data":5})");
  expect(numeric.has_value()) << label;
  if (numeric) {
    expect(std::holds_alternative<ce::v1::json_text>(numeric->data)) << label;
  }
}

template <class C>
void check_decode_string_data(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  for (const auto content_type : {"text/plain"sv, "text/csv"sv, "application/xml"sv,
                                  "TEXT/PLAIN; charset=utf-8"sv}) {
    const std::string document =
        std::string{R"({"specversion":"1.0","id":"1","source":"/s","type":"t","datacontenttype":")"} +
        std::string{content_type} + R"(","data":"hello"})";
    auto decoded = format::decode(document);
    expect(decoded.has_value()) << label << ": " << document;
    if (!decoded) {
      continue;
    }
    expect(std::holds_alternative<std::string>(decoded->data)) << label << ": " << content_type;
    if (std::holds_alternative<std::string>(decoded->data)) {
      expect(std::get<std::string>(decoded->data) == "hello") << label;
    }
  }

  // The same payload round-trips: encode writes a JSON string, decode reads the
  // string back, and the content type is what decides.
  ce::v1::event subject = base_event();
  subject.datacontenttype = "text/plain";
  subject.data = std::string{"hello"};
  auto text = format::encode(subject);
  expect(text.has_value()) << label;
  if (text) {
    auto back = format::decode(*text);
    expect(back.has_value()) << label;
    if (back && std::holds_alternative<std::string>(back->data)) {
      expect(std::get<std::string>(back->data) == "hello") << label;
    }
  }
}

// --- SWR-JSON-0021: both payload members ------------------------------------

template <class C>
void check_data_conflict(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  for (const auto document : {
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data":1,"data_base64":"AA=="})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":"AA==","data":"x"})"sv,
           // Even when one of them is null, both members are present and the
           // peer has carried the payload twice.
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data":null,"data_base64":"AA=="})"sv,
       }) {
    auto decoded = format::decode(document);
    expect(!decoded.has_value()) << label << ": should reject " << document;
    if (!decoded) {
      expect(decoded.error().code == ce::v1::errc::data_conflict) << label << ": " << document;
    }
  }

  // Either one alone is fine, so the rejection is about the pair.
  expect(format::decode(
             R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data":1})")
             .has_value())
      << label;
  expect(bool{format::decode(
             R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":"AA=="})")
             .has_value()})
      << label;
}

// --- SWR-JSON-0022 / 0023 / 0024: extensions --------------------------------

template <class C>
void check_unknown_members_are_extensions(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  auto decoded = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","datacontenttype":"application/json",)"
      R"("dataschema":"https://example.com/s","subject":"sub","time":"2018-04-05T17:31:00Z",)"
      R"("data":{"k":1},"seq":7,"flag":true,"tag":"x"})");
  expect(decoded.has_value()) << label;
  if (!decoded) {
    expect(false) << label << ": " << decoded.error().detail;
    return;
  }

  // Exactly the three unknown members, and none of the context attributes or
  // payload members alongside them.
  expect(decoded->extensions.size() == 3U) << label;
  expect(decoded->extension("seq") != nullptr) << label;
  expect(decoded->extension("flag") != nullptr) << label;
  expect(decoded->extension("tag") != nullptr) << label;
  for (const auto reserved : {"id"sv, "source"sv, "type"sv, "specversion"sv, "datacontenttype"sv,
                              "dataschema"sv, "subject"sv, "time"sv, "data"sv, "data_base64"sv}) {
    expect(decoded->extension(reserved) == nullptr) << label << ": " << reserved;
  }

  // data_base64 is a payload member rather than an extension too.
  auto with_binary = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_base64":"AA==","extra":1})");
  expect(with_binary.has_value()) << label;
  if (with_binary) {
    expect(with_binary->extensions.size() == 1U) << label;
    expect(with_binary->extension("extra") != nullptr) << label;
  }
}

template <class C>
void check_extension_type_mapping(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  auto decoded = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t",)"
      R"("flag":true,"off":false,"seq":7,"neg":-7,"tag":"x","empty":""})");
  expect(decoded.has_value()) << label;
  if (!decoded) {
    return;
  }

  const auto* flag = decoded->extension("flag");
  expect(flag != nullptr) << label;
  if (flag != nullptr) {
    expect(std::holds_alternative<bool>(*flag)) << label;
    if (std::holds_alternative<bool>(*flag)) {
      expect(std::get<bool>(*flag) == true) << label;
    }
  }
  const auto* off = decoded->extension("off");
  if (off != nullptr && std::holds_alternative<bool>(*off)) {
    expect(std::get<bool>(*off) == false) << label;
  }

  const auto* seq = decoded->extension("seq");
  expect(seq != nullptr) << label;
  if (seq != nullptr) {
    // std::int32_t specifically: the CloudEvents Integer type is 32-bit signed.
    expect(std::holds_alternative<std::int32_t>(*seq)) << label;
    if (std::holds_alternative<std::int32_t>(*seq)) {
      expect(std::get<std::int32_t>(*seq) == 7) << label;
    }
  }
  const auto* neg = decoded->extension("neg");
  if (neg != nullptr && std::holds_alternative<std::int32_t>(*neg)) {
    expect(std::get<std::int32_t>(*neg) == -7) << label;
  }

  const auto* tag_value = decoded->extension("tag");
  expect(tag_value != nullptr) << label;
  if (tag_value != nullptr) {
    expect(std::holds_alternative<std::string>(*tag_value)) << label;
    if (std::holds_alternative<std::string>(*tag_value)) {
      expect(std::get<std::string>(*tag_value) == "x") << label;
    }
  }

  // The documented loss: a URI, a URI-reference, a Timestamp and a Binary
  // attribute all come back as std::string, because JSON carries only the text.
  // Recovering the richer type is what the typed extension structs are for.
  ce::v1::event subject = base_event();
  expect(subject.set_extension("schemaurl", ce::v1::uri{"https://example.com/x"}).has_value()) << label;
  auto text = format::encode(subject);
  expect(text.has_value()) << label;
  if (text) {
    auto back = format::decode(*text);
    expect(back.has_value()) << label;
    if (back) {
      const auto* recovered = back->extension("schemaurl");
      expect(recovered != nullptr) << label;
      if (recovered != nullptr) {
        expect(std::holds_alternative<std::string>(*recovered)) << label;
      }
    }
  }

  // A kind with no CloudEvents attribute type is a type mismatch, not a silent
  // drop: an object or an array under an unknown name is not an event.
  //
  // null is NOT in this list. JSON format section 2.2 requires a null attribute
  // to decode as unset, which null-attribute-decodes-as-unset covers.
  for (const auto document : {
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","obj":{"a":1}})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","arr":[1]})"sv,
       }) {
    auto rejected = format::decode(document);
    expect(!rejected.has_value()) << label << ": should reject " << document;
    if (!rejected) {
      expect(rejected.error().code == ce::v1::errc::type_mismatch) << label << ": " << document;
    }
  }
}

template <class C>
void check_floating_extension_rejected(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  for (const auto document : {
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","ratio":1.5})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","ratio":-0.5})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","ratio":1.0})"sv,
       }) {
    auto decoded = format::decode(document);
    expect(!decoded.has_value()) << label << ": should reject " << document;
    if (!decoded) {
      expect(decoded.error().code == ce::v1::errc::type_mismatch)
          << label << ": " << document << " gave " << ce::v1::to_string_view(decoded.error().code);
      // The error names the member, so a caller can say which one was wrong.
      expect(decoded.error().where == "ratio") << label << ": " << decoded.error().where;
    }
  }

  // Rounding is never the answer: the integral-looking 1.0 above is rejected
  // rather than decoded as the Integer 1.
  auto integral = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","ratio":1})");
  expect(integral.has_value()) << label << ": a real Integer is still accepted";
}

// --- SWR-JSON-0026: the empty batch -----------------------------------------

template <class C>
void check_empty_batch(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  for (const auto document : {"[]"sv, " [ ] "sv}) {
    auto decoded = format::decode_batch(document);
    expect(decoded.has_value()) << label << ": " << document;
    if (decoded) {
      expect(decoded->empty()) << label << ": " << document;
    }
  }

  // Encoding no events produces the document that decodes back to no events.
  const std::vector<ce::v1::event> none;
  auto text = format::encode_batch(std::span<const ce::v1::event>{none});
  expect(text.has_value()) << label;
  if (!text) {
    return;
  }
  auto back = format::decode_batch(*text);
  expect(back.has_value()) << label << ": " << *text;
  if (back) {
    expect(back->empty()) << label;
  }
}

// --- SYS-JSON-0001: the spec's own examples ---------------------------------

/// The example event from the CloudEvents core specification, in the JSON event
/// format: a non-JSON payload carried as a JSON string, one string extension and
/// one integer extension.
constexpr auto spec_example = R"({
    "specversion" : "1.0",
    "type" : "com.github.pull_request.opened",
    "source" : "https://github.com/cloudevents/spec/pull",
    "subject" : "123",
    "id" : "A234-1234-1234",
    "time" : "2018-04-05T17:31:00Z",
    "comexampleextension1" : "value",
    "comexampleothervalue" : 5,
    "datacontenttype" : "text/xml",
    "data" : "<much wow=\"xml\"/>"
})"sv;

template <class C>
void check_spec_examples(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  auto decoded = format::decode(spec_example);
  expect(decoded.has_value()) << label;
  if (!decoded) {
    expect(false) << label << ": " << decoded.error().detail;
    return;
  }
  expect(decoded->specversion == "1.0") << label;
  expect(decoded->id == "A234-1234-1234") << label;
  expect(decoded->type == "com.github.pull_request.opened") << label;
  expect(bool{decoded->source == ce::v1::uri_ref{"https://github.com/cloudevents/spec/pull"}}) << label;
  expect(decoded->subject == std::optional<std::string>{"123"}) << label;
  expect(decoded->datacontenttype == std::optional<std::string>{"text/xml"}) << label;
  expect(decoded->time.has_value()) << label;
  if (decoded->time) {
    expect(ce::v1::to_string(*decoded->time) == "2018-04-05T17:31:00Z") << label;
  }
  expect(decoded->extensions.size() == 2U) << label;
  const auto* other = decoded->extension("comexampleothervalue");
  expect(other != nullptr) << label;
  if (other != nullptr && std::holds_alternative<std::int32_t>(*other)) {
    expect(std::get<std::int32_t>(*other) == 5) << label;
  }
  expect(std::holds_alternative<std::string>(decoded->data)) << label;
  if (std::holds_alternative<std::string>(decoded->data)) {
    expect(std::get<std::string>(decoded->data) == R"(<much wow="xml"/>)") << label;
  }

  // Re-encoding and decoding again must land on the same event: whitespace and
  // member order are the codec's business, the event is not.
  auto re_encoded = format::encode(*decoded);
  expect(re_encoded.has_value()) << label;
  if (!re_encoded) {
    return;
  }
  auto again = format::decode(*re_encoded);
  expect(again.has_value()) << label;
  if (again) {
    // Wrapped in bool: ut would otherwise try to print a ce::v1::event on failure.
    expect(bool{*again == *decoded}) << label << ": re-encoding changed the event";
  }

  // The batch representation of the same document, twice over.
  const std::string batch = "[" + std::string{spec_example} + "," + std::string{spec_example} + "]";
  auto batch_decoded = format::decode_batch(batch);
  expect(batch_decoded.has_value()) << label;
  if (!batch_decoded) {
    return;
  }
  expect(batch_decoded->size() == 2U) << label;
  for (const auto& element : *batch_decoded) {
    expect(bool{element == *decoded}) << label << ": batch element differs from the single event";
  }
  auto batch_re_encoded = format::encode_batch(std::span<const ce::v1::event>{*batch_decoded});
  expect(batch_re_encoded.has_value()) << label;
  if (batch_re_encoded) {
    auto batch_again = format::decode_batch(*batch_re_encoded);
    expect(batch_again.has_value()) << label;
    if (batch_again) {
      expect(bool{*batch_again == *batch_decoded}) << label;
    }
  }

  // A malformed element fails the whole batch rather than being skipped.
  const std::string bad_batch = "[" + std::string{spec_example} +
                                R"(,{"specversion":"1.0","source":"/s","type":"t"}])";
  auto rejected_batch = format::decode_batch(bad_batch);
  expect(!rejected_batch.has_value()) << label;
  if (!rejected_batch) {
    expect(rejected_batch.error().code == ce::v1::errc::missing_required_attribute) << label;
  }
}

// --- suites ------------------------------------------------------------------
//
// Each one runs the shared checks against both codecs.

// The entry points are also what SYS-JSON-0001 names, so the spec's own example
// document -- single and batched -- is exercised through them here.
const boost::ut::suite<"json-format-entry-points"> format_entry_points = [] {
  using namespace boost::ut;

  "the four operations have the declared signatures"_test = [] {
    using format = ce::v1::json_format<nlohmann_codec>;
    static_assert(std::is_same_v<decltype(format::encode(std::declval<const ce::v1::event&>())),
                                 ce::v1::result<std::string>>);
    static_assert(std::is_same_v<decltype(format::decode(""sv)), ce::v1::result<ce::v1::event>>);
    static_assert(
        std::is_same_v<decltype(format::encode_batch(std::declval<std::span<const ce::v1::event>>())),
                       ce::v1::result<std::string>>);
    static_assert(
        std::is_same_v<decltype(format::decode_batch(""sv)), ce::v1::result<std::vector<ce::v1::event>>>);
    static_assert(std::is_same_v<decltype(ce::v1::json_format<mini_codec>::decode(""sv)),
                                 ce::v1::result<ce::v1::event>>);
    expect(true);
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_entry_points<C>(codec); };
  });

  "the spec's example event, single and batched, with nlohmann_codec"_test = [] {
    check_spec_examples<nlohmann_codec>("nlohmann_codec");
  };
  "the spec's example event, single and batched, with mini_codec"_test = [] {
    check_spec_examples<mini_codec>("mini_codec");
  };
};

const boost::ut::suite<"integer-attribute-as-json-number"> integer_attribute_as_number = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_integer_attribute<C>(codec); };
  });
};

const boost::ut::suite<"integer-attribute-out-of-range-or-fractional-is-error">
    integer_attribute_rejections = [] {
      using namespace boost::ut;

      ce_test::for_each_codec([]<class C>(std::string_view codec) {
        test(std::string{codec}) = [codec] { check_integer_rejections<C>(codec); };
      });
    };

const boost::ut::suite<"binary-attribute-as-base64-string"> binary_attribute_as_base64 = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_binary_attribute<C>(codec); };
  });
};

const boost::ut::suite<"uri-uriref-timestamp-as-json-string"> textual_attributes = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_textual_attributes<C>(codec); };
  });
};

const boost::ut::suite<"json-text-data-under-data-member"> json_text_data = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_json_text_data<C>(codec); };
  });
};

const boost::ut::suite<"binary-data-under-data-base64-member"> binary_data = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_binary_data<C>(codec); };
  });
};

const boost::ut::suite<"string-data-under-data-member-as-json-string"> string_data = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_string_data<C>(codec); };
  });
};

const boost::ut::suite<"decode-data-base64-yields-binary"> decode_data_base64 = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_decode_data_base64<C>(codec); };
  });
};

const boost::ut::suite<"decode-data-yields-json-text"> decode_data_json_text = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_decode_data_as_json_text<C>(codec); };
  });
};

template <class C>
void check_extension_name_grammar(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  for (const auto document : {
           // The underscore is the shape a round-trip fuzzer reached first.
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data_bae6s4":"AAEC"})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","Upper":"x"})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","has-dash":"x"})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","has space":"x"})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","":"x"})"sv,
       }) {
    auto decoded = format::decode(document);
    expect(!decoded.has_value()) << label << ": should reject " << document;
    if (!decoded) {
      expect(decoded.error().code == ce::v1::errc::invalid_attribute_name) << label << ": " << document;
    }
  }

  // An attribute that is present but empty is a second way to reach an event
  // that would not validate.
  {
    auto empty_type = format::decode(
        R"({"specversion":"1.0","id":"1","source":"/s","type":""})");
    expect(!empty_type.has_value()) << label << ": empty type";
  }

  // A legal name is still accepted, so the rejection is about the grammar.
  auto ok = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","seq9":"x"})");
  expect(ok.has_value()) << label;

  // The invariant the fuzzer asserts: anything decode returns validates, so a
  // document that decodes can always be encoded again.
  if (ok) {
    expect(ok->validate().has_value()) << label;
    expect(format::encode(*ok).has_value()) << label;
  }
}

const boost::ut::suite<"decode-data-string-with-non-json-content-type-yields-string">
    decode_string_data = [] {
      using namespace boost::ut;

      ce_test::for_each_codec([]<class C>(std::string_view codec) {
        test(std::string{codec}) = [codec] { check_decode_string_data<C>(codec); };
      });
    };

const boost::ut::suite<"decode-both-data-members-is-error"> both_data_members = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_data_conflict<C>(codec); };
  });
};

const boost::ut::suite<"unknown-top-level-members-become-extensions"> unknown_members = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_unknown_members_are_extensions<C>(codec); };
  });
};

const boost::ut::suite<"extension-decode-type-mapping"> extension_type_mapping = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_extension_type_mapping<C>(codec); };
  });
};

const boost::ut::suite<"floating-point-extension-value-is-type-mismatch"> floating_extension = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_floating_extension_rejected<C>(codec); };
  });
};

const boost::ut::suite<"json-format-media-types"> media_types = [] {
  using namespace boost::ut;

  // Constants, not accessors: the HTTP binding matches on them at compile time.
  "the two media types are fixed and codec-independent"_test = [] {
    static_assert(ce::v1::json_format<nlohmann_codec>::content_type ==
                  "application/cloudevents+json"sv);
    static_assert(ce::v1::json_format<nlohmann_codec>::batch_content_type ==
                  "application/cloudevents-batch+json"sv);
    static_assert(ce::v1::json_format<mini_codec>::content_type == "application/cloudevents+json"sv);
    static_assert(ce::v1::json_format<mini_codec>::batch_content_type ==
                  "application/cloudevents-batch+json"sv);
    // The format re-exports what the JSON layer declares, so there is one
    // definition rather than two that can drift.
    static_assert(ce::v1::json_format<mini_codec>::content_type == ce::v1::json::content_type);
    static_assert(ce::v1::json_format<mini_codec>::batch_content_type == ce::v1::json::batch_content_type);
    expect(true);
  };

  "both are JSON media types by the SDK's own test"_test = [] {
    expect(ce::v1::is_json_content_type(ce::v1::json_format<nlohmann_codec>::content_type));
    expect(ce::v1::is_json_content_type(ce::v1::json_format<mini_codec>::batch_content_type));
  };
};

const boost::ut::suite<"empty-batch-decodes-to-no-events"> empty_batch = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_empty_batch<C>(codec); };
  });
};

const boost::ut::suite<"decoded-event-always-validates"> extension_name_grammar = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_extension_name_grammar<C>(codec); };
  });
};

template <class C>
void check_null_is_unset(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  // The shape the JSON format specification's own example uses.
  auto decoded = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","unsetextension":null})");
  expect(decoded.has_value()) << label;
  if (decoded) {
    expect(decoded->extension("unsetextension") == nullptr)
        << label << ": a null extension must not become an attribute";
    expect(decoded->extensions.empty()) << label;
    expect(decoded->validate().has_value()) << label;
  }

  // A null OPTIONAL context attribute is unset too, and an absent one and an
  // explicitly null one decode to the same event.
  auto explicit_null = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","subject":null,"time":null})");
  auto omitted = format::decode(R"({"specversion":"1.0","id":"1","source":"/s","type":"t"})");
  expect(explicit_null.has_value()) << label;
  expect(omitted.has_value()) << label;
  if (explicit_null && omitted) {
    expect(bool{*explicit_null == *omitted}) << label;
    expect(!explicit_null->subject.has_value()) << label;
    expect(!explicit_null->time.has_value()) << label;
  }

  // data is the documented exception: an explicit null payload is distinct from
  // an absent one, so it is NOT swallowed by the unset rule.
  auto null_data = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","data":null})");
  expect(null_data.has_value()) << label;

  // A non-null extension beside a null one still arrives.
  auto mixed = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","gone":null,"kept":"x"})");
  expect(mixed.has_value()) << label;
  if (mixed) {
    expect(mixed->extension("gone") == nullptr) << label;
    expect(mixed->extension("kept") != nullptr) << label;
    expect(mixed->extensions.size() == 1_ul) << label;
  }
}

const boost::ut::suite<"null-attribute-decodes-as-unset"> null_is_unset = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_null_is_unset<C>(codec); };
  });
};

/// \brief An extension integer beyond int64 must not decode to some other number.
template <class C>
void check_out_of_range_integer_extension(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v1::json_format<C>;

  // 18446744073709551615 reinterprets as -1, which is INSIDE the CloudEvents
  // Integer range, so the format layer's own bounds check does not catch it.
  // Before the codec refused it, this decoded to an extension holding -1.
  for (const auto document : {
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","n":9223372036854775808})"sv,
           R"({"specversion":"1.0","id":"1","source":"/s","type":"t","n":18446744073709551615})"sv,
       }) {
    auto decoded = format::decode(document);
    expect(!decoded.has_value()) << label << ": should reject " << document;
    if (decoded) {
      // Name the value, so a regression says what it produced rather than only
      // that it produced something.
      const auto* held = decoded->extension("n");
      if (held != nullptr && std::holds_alternative<std::int32_t>(*held)) {
        expect(false) << label << ": decoded to " << std::get<std::int32_t>(*held);
      }
      continue;
    }
    // Either route is conformant, and the two in-tree codecs take one each:
    // nlohmann parses the number and refuses it at as_int (out_of_range), while
    // mini_codec's hand-written parser refuses the document (parse_error). What
    // the concept forbids is a value, which is asserted above.
    const auto code = decoded.error().code;
    expect(code == ce::v1::errc::out_of_range || code == ce::v1::errc::parse_error)
        << label << ": " << document << " gave " << ce::v1::to_string_view(code);
  }

  // A value inside the Integer range still decodes, so the refusal is about the
  // range and not about extensions that happen to be numbers.
  auto fine = format::decode(
      R"({"specversion":"1.0","id":"1","source":"/s","type":"t","n":42})");
  expect(fine.has_value()) << label;
  if (fine) {
    const auto* held = fine->extension("n");
    expect(held != nullptr) << label;
    if (held != nullptr && std::holds_alternative<std::int32_t>(*held)) {
      expect(std::get<std::int32_t>(*held) == 42) << label;
    }
  }
}

const boost::ut::suite<"extension-integer-beyond-int64-is-refused"> beyond_int64 = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_out_of_range_integer_extension<C>(codec); };
  });
};

}  // namespace

const boost::ut::suite<"format-rules-shared-with-the-bench"> format_rules_shared = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] {
      ce::v1::checks::check_format_rules<C>(
          [codec](bool ok, std::string_view what) { expect(ok) << codec << ": " << what; });
    };
  });
};

int main() {}
