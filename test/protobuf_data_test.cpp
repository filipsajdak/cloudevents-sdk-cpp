#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/result.hpp>

#include "protobuf_support.hpp"

// The three data fields: what the encoder writes to each and what the decoder makes of them.

namespace {

using namespace std::string_view_literals;
using namespace ce::literals;
using namespace ce::test::pb;

}  // namespace

// spec: SWR-PROTO-0007
const boost::ut::suite<"protobuf-data-write-arms"> data_write = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "a std::string payload is written to text_data"_test = [] {
    expect(encoded_form(base_event({.data = std::string{"hello"}})) ==
           event_bytes({text(text_data_field, "hello")}));
  };

  "an empty std::string payload is a text_data of length 0"_test = [] {
    expect(encoded_form(base_event({.data = std::string{}})) ==
           event_bytes({text(text_data_field, "")}));
  };

  "a json_text payload is text"_test = [] {
    expect(encoded_form(base_event({.data = ce::json_text{.raw = R"({"a":1})"}})) ==
           event_bytes({text(text_data_field, R"({"a":1})")}));
  };

  "a binary payload is written to binary_data, also when empty"_test = [] {
    expect(encoded_form(base_event({.data = ce::binary{std::byte{7}}})) ==
           event_bytes({text(binary_data_field, "\x07"sv)}));
    expect(encoded_form(base_event({.data = ce::binary{}})) ==
           event_bytes({text(binary_data_field, "")}));
  };

  "no payload writes no data field"_test = [] {
    expect(encoded_form(base_event()) == event_bytes());
  };

  "proto_data is only for a binary payload of application/protobuf with a dataschema"_test = [] {
    const auto payload = ce::binary{std::byte{1}};
    const auto no_schema = encoded_form(
        base_event({.datacontenttype = "application/protobuf"_mediatype, .data = payload}));
    expect(no_schema == event_bytes({string_entry("datacontenttype", "application/protobuf"),
                                     text(binary_data_field, "\x01"sv)}));
    const auto other_type = encoded_form(base_event({
        .datacontenttype = "application/octet-stream"_mediatype,
        .dataschema = "type.example/T"_dataschema,
        .data = payload,
    }));
    expect(other_type == event_bytes({string_entry("datacontenttype", "application/octet-stream"),
                                      entry("dataschema", text(ce_uri, "type.example/T")),
                                      text(binary_data_field, "\x01"sv)}));
    const auto as_text = encoded_form(base_event({
        .datacontenttype = "application/protobuf"_mediatype,
        .dataschema = "type.example/T"_dataschema,
        .data = std::string{"x"},
    }));
    expect(as_text == event_bytes({string_entry("datacontenttype", "application/protobuf"),
                                   entry("dataschema", text(ce_uri, "type.example/T")),
                                   text(text_data_field, "x")}));
  };

  "the dataschema is the type URL verbatim, and also an attribute of its own"_test = [] {
    const auto written = encoded_form(base_event({
        .datacontenttype = "application/protobuf"_mediatype,
        .dataschema = "type.googleapis.com/pkg.Thing"_dataschema,
        .data = ce::binary{std::byte{1}},
    }));
    expect(written ==
           event_bytes(
               {string_entry("datacontenttype", "application/protobuf"),
                entry("dataschema", text(ce_uri, "type.googleapis.com/pkg.Thing")),
                message(proto_data_field, any_body("type.googleapis.com/pkg.Thing", "\x01"sv))}));
  };

  "a dataschema that is not a URI-reference is not written as a type URL"_test = [] {
    const auto spaced = base_event({.datacontenttype = "application/protobuf"_mediatype,
                                    .dataschema = "not a uri"_dataschema,
                                    .data = ce::binary{std::byte{1}}});
    expect(encoded_form(spaced) ==
           event_bytes({string_entry("datacontenttype", "application/protobuf"),
                        entry("dataschema", text(ce_uri, "not a uri")),
                        text(binary_data_field, "\x01"sv)}));
  };
};

// spec: SWR-PROTO-0008
const boost::ut::suite<"protobuf-data-read-arms"> data_read = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "text_data is a std::string holding the bytes unchanged"_test = [] {
    const auto read = decoded(event_bytes({text(text_data_field, "h\xC3\xA9llo")}));
    expect(read.has_value() && std::get<std::string>(read->data()) == "h\xC3\xA9llo"sv);
  };

  "an empty text_data is an empty std::string"_test = [] {
    const auto read = decoded(event_bytes({text(text_data_field, "")}));
    expect(read.has_value() && std::holds_alternative<std::string>(read->data()));
  };

  "binary_data is a binary"_test = [] {
    const auto read = decoded(event_bytes({text(binary_data_field, "\x00\xFF"sv)}));
    expect(read.has_value() &&
           std::get<ce::binary>(read->data()) == ce::binary{std::byte{0}, std::byte{0xFF}});
  };

  "an empty binary_data without a datacontenttype is no payload"_test = [] {
    const auto read = decoded(event_bytes({text(binary_data_field, "")}));
    expect(read.has_value() && std::holds_alternative<std::monostate>(read->data()));
  };

  "an empty binary_data with a datacontenttype is an empty binary"_test = [] {
    const auto read = decoded(
        event_bytes({string_entry("datacontenttype", "text/plain"), text(binary_data_field, "")}));
    expect(read.has_value() && std::holds_alternative<ce::binary>(read->data()) &&
           std::get<ce::binary>(read->data()).empty());
  };

  "no data field is no payload"_test = [] {
    const auto read = decoded(event_bytes());
    expect(read.has_value() && std::holds_alternative<std::monostate>(read->data()));
  };

  "the data field that appears last wins"_test = [] {
    const auto text_last =
        decoded(event_bytes({text(binary_data_field, "b"), text(text_data_field, "t")}));
    expect(text_last.has_value() && std::holds_alternative<std::string>(text_last->data()));
    const auto binary_last =
        decoded(event_bytes({text(text_data_field, "t"), text(binary_data_field, "b")}));
    expect(binary_last.has_value() && std::holds_alternative<ce::binary>(binary_last->data()));
    const auto any_last = decoded(event_bytes(
        {text(text_data_field, "t"), message(proto_data_field, any_body("x.y/T", "v"))}));
    expect(any_last.has_value() && std::holds_alternative<ce::binary>(any_last->data()));
    const auto text_after_any = decoded(event_bytes(
        {message(proto_data_field, any_body("x.y/T", "v")), text(text_data_field, "t")}));
    expect(text_after_any.has_value() &&
           std::holds_alternative<std::string>(text_after_any->data()) &&
           !text_after_any->dataschema().has_value());
  };

  "proto_data is a binary payload, with datacontenttype and dataschema filled when absent"_test =
      [] {
        const auto read = decoded(event_bytes(
            {message(proto_data_field,
                     any_body("type.googleapis.com/google.protobuf.Struct", "\x0A\x00"sv))}));
        expect(read.has_value());
        if (read) {
          expect(std::get<ce::binary>(read->data()) == ce::binary{std::byte{0x0A}, std::byte{0}});
          expect(read->datacontenttype() &&
                 read->datacontenttype()->view() == "application/protobuf"sv);
          expect(read->dataschema() &&
                 read->dataschema()->view() == "type.googleapis.com/google.protobuf.Struct"sv);
        }
      };

  "proto_data keeps a datacontenttype and a dataschema that are present"_test = [] {
    const auto read = decoded(event_bytes({
        string_entry("datacontenttype", "application/x-thing"),
        entry("dataschema", text(ce_uri, "https://example.com/own")),
        message(proto_data_field, any_body("other.example/T", "v")),
    }));
    expect(read.has_value());
    if (read) {
      expect(read->datacontenttype()->view() == "application/x-thing"sv);
      expect(read->dataschema()->view() == "https://example.com/own"sv);
    }
  };

  "an empty type URL counts as absent"_test = [] {
    const auto absent =
        decoded(event_bytes({message(proto_data_field, text(any_value_field, "v"))}));
    expect(absent.has_value() && !absent->dataschema().has_value());
    const auto empty = decoded(event_bytes({message(proto_data_field, any_body("", "v"))}));
    expect(empty.has_value() && !empty->dataschema().has_value());
  };

  "a type URL that is not a URI-reference is a parse_error"_test = [] {
    for (const auto url : {"has space"sv,
                           "bad%zzescape"sv,
                           "100%"sv,
                           "a<b"sv,
                           "two#frag#ments"sv,
                           "caf\xC3\xA9"sv}) {
      expect(refused(event_bytes({message(proto_data_field, any_body(url, "v"))}),
                     ce::errc::parse_error))
          << url;
    }
  };

  "the URI-reference check accepts the characters of RFC 3986 and no others"_test = [] {
    namespace detail = ce::protobuf::detail;
    static_assert(detail::valid_uri_reference(""));
    static_assert(detail::valid_uri_reference("https://user@host:8080/a/b?c=d&e=f#frag"));
    static_assert(detail::valid_uri_reference("type.googleapis.com/google.protobuf.Struct"));
    static_assert(detail::valid_uri_reference("-._~:/?#[]@!$&'()*+,;="));
    static_assert(detail::valid_uri_reference("%00%7f%7F%aB%Cd%ef%09"));
    expect(detail::valid_uri_reference("%aF%Af%fa%0a%9F%A0%f0"));
    for (const char accepted : {'a', 'z', 'A', 'Z', '0', '9'}) {
      expect(detail::valid_uri_reference(std::string_view{&accepted, 1})) << accepted;
    }
    // the characters just outside each range of letters and digits, and the ones RFC 3986 excludes
    for (const char refused_character :
         {'`', '{', ' ', '"', '<', '>', '\\', '^', '|', '}', '\x7F', '\0'}) {
      expect(!detail::valid_uri_reference(std::string_view{&refused_character, 1}))
          << static_cast<int>(refused_character);
    }
    for (const auto bad : {"%"sv,
                           "%4"sv,
                           "%g0"sv,
                           "%0g"sv,
                           "%G0"sv,
                           "%/0"sv,
                           "%:0"sv,
                           "%@0"sv,
                           "%`0"sv,
                           "%0{"sv,
                           "a#b#c"sv,
                           "a b"sv,
                           "\xC3\xA9"sv}) {
      expect(!detail::valid_uri_reference(bad)) << bad;
    }
  };

  "a type URL with a valid percent escape and a fragment is accepted"_test = [] {
    expect(decoded(event_bytes({message(proto_data_field, any_body("t.example/a%20b?q=1#f", "v"))}))
               .has_value());
  };

  "an Any with an unknown field is read and one with a type URL that is not UTF-8 is refused"_test =
      [] {
        expect(decoded(event_bytes(
                           {message(proto_data_field,
                                    concat({text(unknown_field, "x"), varint(unknown_field, 3)}))}))
                   .has_value());
        expect(refused(event_bytes({message(proto_data_field, text(type_url_field, "\xFF"sv))}),
                       ce::errc::invalid_utf8));
      };
};

int main() {}
