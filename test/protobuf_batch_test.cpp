#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/result.hpp>

#include "mini_codec.hpp"
#include "protobuf_support.hpp"

// The batch, the JSON classification and the proto_data side channel: the three additions the
// owner ruled on 2026-10-25 (CR-0009 items 8, 9, 10, 20, 21 and 22).

namespace {

using namespace std::string_view_literals;
using namespace ce::literals;
using namespace ce::test::pb;
using protobuf = ce::protobuf_format;
using codec = ce::test::mini_codec;

[[nodiscard]] auto batch_of(std::initializer_list<ce::binary> events) -> ce::binary {
  ce::binary out;
  for (const auto& body : events) {
    out = concat({out, message(batch_events_field, body)});
  }
  return out;
}

[[nodiscard]] auto decoded_batch(const ce::binary& encoded,
                                 ce::protobuf::decode_options options = {})
    -> ce::result<std::vector<ce::event>> {
  return protobuf::decode_batch(encoded, options);
}

[[nodiscard]] auto batch_refused(const ce::binary& encoded,
                                 ce::errc code,
                                 ce::protobuf::decode_options options = {}) -> bool {
  const auto read = decoded_batch(encoded, options);
  return !read.has_value() && read.error().code == code;
}

[[nodiscard]] auto media(std::string_view media_type) -> std::optional<ce::datacontenttype> {
  auto made = ce::datacontenttype::make(media_type);
  return made ? std::optional{std::move(*made)} : std::nullopt;
}

[[nodiscard]] auto schema(std::string_view url) -> std::optional<ce::dataschema> {
  auto made = ce::dataschema::make(url);
  return made ? std::optional{std::move(*made)} : std::nullopt;
}

[[nodiscard]] auto json_event(std::string_view media_type, ce::data_t payload) -> ce::event {
  return base_event({.datacontenttype = media(media_type), .data = std::move(payload)});
}

}  // namespace

// spec: SWR-PROTO-0024
const boost::ut::suite<"protobuf-batch-round-trip"> batch_round_trip = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  const auto first = base_event({.subject = "one"_subject});
  const auto second = ce::event{"2"_id, "/other"_source, "t2"_type, {.data = std::string{"two"}}};

  "a batch is a CloudEventBatch of repeated events in field 1"_test = [&] {
    const std::vector<ce::event> events{first, second};
    const auto written = protobuf::encode_batch(events);
    expect(written.has_value());
    expect(*written == batch_of({encoded_form(first), encoded_form(second)}));
    const auto read = decoded_batch(*written);
    expect(read.has_value() && read->size() == 2U);
    if (read && read->size() == 2U) {
      expect((*read)[0] == first);
      expect((*read)[1] == second);
    }
  };

  "an empty batch is empty bytes and decodes to no events"_test = [] {
    const auto written = protobuf::encode_batch({});
    expect(written.has_value() && written->empty());
    const auto read = decoded_batch({});
    expect(read.has_value() && read->empty());
  };

  "a field of the batch that is not an event is skipped"_test = [&] {
    const auto read = decoded_batch(concat({text(unknown_field, "x"),
                                            message(batch_events_field, encoded_form(first)),
                                            varint(unknown_field, 1)}));
    expect(read.has_value() && read->size() == 1U);
  };

  "a batch that is cut short is a parse_error and a bad event keeps its own code"_test = [&] {
    auto cut = batch_of({encoded_form(first)});
    cut.pop_back();
    expect(batch_refused(cut, ce::errc::parse_error));
    expect(batch_refused(batch_of({encoded_form(first), event_bytes({string_entry("n", "\xFF")})}),
                         ce::errc::invalid_utf8));
    expect(batch_refused(batch_of({ce::binary{}}), ce::errc::unsupported_spec_version));
  };
};

// spec: SWR-PROTO-0024
const boost::ut::suite<"protobuf-batch-media-type"> batch_media_type = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the media type of a batch is application/cloudevents-batch+protobuf"_test = [] {
    expect(protobuf::batch_content_type == "application/cloudevents-batch+protobuf"sv);
    expect(ce::protobuf::batch_content_type == protobuf::batch_content_type);
    expect(protobuf::content_type != protobuf::batch_content_type);
  };
};

// spec: SWR-PROTO-0024
// spec: SWR-PROTO-0010
const boost::ut::suite<"protobuf-batch-count-limit"> batch_count_limit = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  const auto body = event_bytes();

  "the default is the owner's proposed 100,000 events"_test = [] {
    expect(ce::protobuf::default_max_batch_event_count == 100'000U);
    expect(ce::protobuf::decode_options{}.max_batch_event_count ==
           ce::protobuf::default_max_batch_event_count);
  };

  "more events than the limit is out_of_range, as many is read"_test = [&] {
    const auto three = batch_of({body, body, body});
    expect(decoded_batch(three, {.max_batch_event_count = 3}).has_value());
    expect(batch_refused(three, ce::errc::out_of_range, {.max_batch_event_count = 2}));
    expect(batch_refused(three, ce::errc::out_of_range, {.max_batch_event_count = 0}));
  };

  "the count is checked before any event is decoded"_test = [] {
    const auto malformed = batch_of({ce::binary{}, ce::binary{}, ce::binary{}});
    expect(batch_refused(malformed, ce::errc::out_of_range, {.max_batch_event_count = 2}));
    expect(
        batch_refused(malformed, ce::errc::unsupported_spec_version, {.max_batch_event_count = 3}));
  };

  "a batch above the default limit is out_of_range before any event is read"_test = [] {
    ce::binary empty_events;
    for (std::size_t index = 0; index <= ce::protobuf::default_max_batch_event_count; ++index) {
      wire::put_field(empty_events, batch_events_field, ce::binary{});
    }
    expect(batch_refused(empty_events, ce::errc::out_of_range));
  };

  "the size and attribute limits apply to each event, not to the batch"_test = [&] {
    const auto two = batch_of({body, body});
    expect(decoded_batch(two, {.max_encoded_size = body.size()}).has_value());
    expect(batch_refused(batch_of({body, event_bytes({string_entry("a", "1")})}),
                         ce::errc::out_of_range,
                         {.max_encoded_size = body.size()}));
    const auto crowded = event_bytes({string_entry("a", "1"), string_entry("b", "2")});
    expect(decoded_batch(batch_of({crowded, crowded}), {.max_attribute_count = 2}).has_value());
    expect(batch_refused(
        batch_of({body, crowded}), ce::errc::out_of_range, {.max_attribute_count = 1}));
  };
};

// spec: SWR-PROTO-0025
const boost::ut::suite<"protobuf-decode-as-classifies-json"> decode_as_json = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  const auto count_of = [](const ce::protobuf::decoded_json<codec>& read) -> std::int64_t {
    const auto* member = codec::find(*read.payload, "count");
    if (member == nullptr) {
      return -1;
    }
    const auto number = codec::as_int(*member);
    return number ? *number : -1;
  };

  "text_data of a JSON content type is parsed and stays text in the event"_test = [&] {
    const auto written =
        encoded_form(json_event("application/json", std::string{R"({"count":3})"}));
    const auto read = protobuf::decode_as<codec>(written);
    expect(read.has_value() && read->payload.has_value());
    if (read && read->payload) {
      expect(count_of(*read) == 3);
      expect(std::get<std::string>(read->event.data()) == R"({"count":3})"sv);
    }
  };

  "binary_data of a +json content type is parsed too"_test = [&] {
    const auto written =
        encoded_form(json_event("application/cloudevents+json", ce::to_bytes(R"({"count":4})")));
    const auto read = protobuf::decode_as<codec>(written);
    expect(read.has_value() && read->payload.has_value());
    if (read && read->payload) {
      expect(count_of(*read) == 4);
      expect(std::holds_alternative<ce::binary>(read->event.data()));
    }
  };

  "a payload that is not JSON-typed is not parsed"_test = [] {
    for (const auto media_type : {"text/plain"sv, "application/xml"sv}) {
      const auto written = encoded_form(json_event(media_type, std::string{"{\"count\":3}"}));
      const auto read = protobuf::decode_as<codec>(written);
      expect(read.has_value() && !read->payload.has_value()) << media_type;
    }
    const auto untyped = protobuf::decode_as<codec>(
        encoded_form(base_event({.data = std::string{"{\"count\":3}"}})));
    expect(untyped.has_value() && !untyped->payload.has_value());
  };

  "an event with no payload or an unclassified one has no value"_test = [] {
    const auto none = protobuf::decode_as<codec>(
        encoded_form(base_event({.datacontenttype = "application/json"_mediatype})));
    expect(none.has_value() && !none->payload.has_value());
  };

  "JSON that does not parse is a parse_error naming the data"_test = [] {
    const auto read = protobuf::decode_as<codec>(
        encoded_form(json_event("application/json", std::string{"{not json"})));
    expect(!read.has_value() && read.error().code == ce::errc::parse_error &&
           read.error().where == "data"sv);
  };

  "decode_as decodes as decode does, with the same limits and errors"_test = [] {
    expect(!protobuf::decode_as<codec>(ce::binary{}).has_value());
    const auto bytes = event_bytes();
    const auto limited = protobuf::decode_as<codec>(bytes, {.max_encoded_size = bytes.size() - 1});
    expect(!limited.has_value() && limited.error().code == ce::errc::out_of_range);
  };
};

// spec: SWR-PROTO-0025
const boost::ut::suite<"protobuf-default-decode-keeps-text"> default_keeps_text = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "decode returns a JSON-typed text_data as a std::string, not as JSON"_test = [] {
    const auto written =
        encoded_form(json_event("application/json", std::string{R"({"count":3})"}));
    const auto read = protobuf::decode(written);
    expect(read.has_value() && std::holds_alternative<std::string>(read->data()));
  };

  "a json_text and a json_document are written as text without a codec argument"_test = [] {
    const auto raw = std::string{R"({"count":3})"};
    const auto as_text = encoded_form(json_event("application/json", ce::json_text{.raw = raw}));
    expect(as_text == encoded_form(json_event("application/json", raw)));
    const auto parsed = codec::parse(raw);
    expect(parsed.has_value());
    if (parsed) {
      const auto as_document =
          encoded_form(json_event("application/json", ce::json_document::make<codec>(*parsed)));
      const auto read = protobuf::decode(as_document);
      expect(read.has_value() && std::holds_alternative<std::string>(read->data()));
      expect(read.has_value() && std::get<std::string>(read->data()) == codec::dump(*parsed));
    }
  };
};

// spec: SWR-PROTO-0026
const boost::ut::suite<"protobuf-proto-data-write-rule"> proto_data_write = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  const auto proto_event = [](std::string_view url) {
    return base_event({.datacontenttype = "application/protobuf"_mediatype,
                       .dataschema = schema(url),
                       .data = ce::binary{std::byte{1}}});
  };

  "encode_proto_data writes the URL given and the dataschema as its own attribute"_test = [&] {
    const auto written = protobuf::encode_proto_data(proto_event("https://schemas.example/own"),
                                                     "type.googleapis.com/pkg.Thing");
    expect(written.has_value());
    expect(*written ==
           event_bytes(
               {string_entry("datacontenttype", "application/protobuf"),
                entry("dataschema", text(ce_uri, "https://schemas.example/own")),
                message(proto_data_field, any_body("type.googleapis.com/pkg.Thing", "\x01"sv))}));
  };

  "it writes proto_data whatever the content type and whether or not there is a dataschema"_test =
      [] {
        const auto plain = base_event({.data = ce::binary{std::byte{1}}});
        const auto written = protobuf::encode_proto_data(plain, "t.example/T");
        expect(written.has_value());
        expect(*written ==
               event_bytes({message(proto_data_field, any_body("t.example/T", "\x01"sv))}));
      };

  "an empty URL counts as absent: the Any carries the value alone"_test = [&] {
    const auto written = protobuf::encode_proto_data(proto_event("t.example/T"), "");
    expect(written.has_value());
    expect(*written == event_bytes({string_entry("datacontenttype", "application/protobuf"),
                                    entry("dataschema", text(ce_uri, "t.example/T")),
                                    message(proto_data_field, text(any_value_field, "\x01"sv))}));
  };

  "a URL that is not a URI-reference is a parse_error"_test = [&] {
    const auto written = protobuf::encode_proto_data(proto_event("t.example/T"), "has space");
    expect(!written.has_value() && written.error().code == ce::errc::parse_error);
  };

  "a payload that is not binary is a type_mismatch"_test = [] {
    for (const auto& event : {base_event(), base_event({.data = std::string{"x"}})}) {
      const auto written = protobuf::encode_proto_data(event, "t.example/T");
      expect(!written.has_value() && written.error().code == ce::errc::type_mismatch);
    }
  };
};

// spec: SWR-PROTO-0026
const boost::ut::suite<"protobuf-proto-data-read-rule"> proto_data_read = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "decode_detailed returns the event and the type URL of the proto_data value"_test = [] {
    const auto read = protobuf::decode_detailed(
        event_bytes({entry("dataschema", text(ce_uri, "https://schemas.example/own")),
                     message(proto_data_field, any_body("type.googleapis.com/pkg.Thing", "v"))}));
    expect(read.has_value() && read->type_url.has_value());
    if (read && read->type_url) {
      expect(*read->type_url == "type.googleapis.com/pkg.Thing"sv);
      expect(read->event.dataschema()->view() == "https://schemas.example/own"sv);
      expect(std::holds_alternative<ce::binary>(read->event.data()));
    }
  };

  "the type URL is absent for any other data and for an empty URL"_test = [] {
    for (const auto& encoded :
         {event_bytes(),
          event_bytes({text(text_data_field, "t")}),
          event_bytes({text(binary_data_field, "b")}),
          event_bytes({message(proto_data_field, text(any_value_field, "v"))}),
          event_bytes({message(proto_data_field, any_body("t.example/T", "v")),
                       text(text_data_field, "t")})}) {
      const auto read = protobuf::decode_detailed(encoded);
      expect(read.has_value() && !read->type_url.has_value());
    }
  };

  "decode_detailed decodes as decode does, with the same limits and errors"_test = [] {
    const auto plain =
        protobuf::decode(event_bytes({message(proto_data_field, any_body("t.example/T", "v"))}));
    const auto detailed = protobuf::decode_detailed(
        event_bytes({message(proto_data_field, any_body("t.example/T", "v"))}));
    expect(plain.has_value() && detailed.has_value() && *plain == detailed->event);
    const auto bad = protobuf::decode_detailed(
        event_bytes({message(proto_data_field, any_body("has space", "v"))}));
    expect(!bad.has_value() && bad.error().code == ce::errc::parse_error);
    expect(!protobuf::decode_detailed(ce::binary{}).has_value());
    const auto bytes = event_bytes();
    const auto limited = protobuf::decode_detailed(bytes, {.max_encoded_size = bytes.size() - 1});
    expect(!limited.has_value() && limited.error().code == ce::errc::out_of_range);
  };
};

// spec: SWR-PROTO-0026
const boost::ut::suite<"protobuf-proto-data-type-url-side-channel"> proto_data_side_channel = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the URL written through the side channel is the URL read back, beside a different dataschema"_test =
      [] {
        const auto sent = base_event({.datacontenttype = "application/protobuf"_mediatype,
                                      .dataschema = "https://schemas.example/own"_dataschema,
                                      .data = ce::binary{std::byte{9}}});
        const auto written = protobuf::encode_proto_data(sent, "type.googleapis.com/pkg.Thing");
        expect(written.has_value());
        const auto read = protobuf::decode_detailed(*written);
        expect(read.has_value() && read->type_url.has_value());
        if (read && read->type_url) {
          expect(*read->type_url == "type.googleapis.com/pkg.Thing"sv);
          expect(read->event == sent);
        }
      };

  "the plain encoder writes the dataschema as the type URL, so the URL is the dataschema"_test =
      [] {
        const auto sent = base_event({.datacontenttype = "application/protobuf"_mediatype,
                                      .dataschema = "type.googleapis.com/pkg.Thing"_dataschema,
                                      .data = ce::binary{std::byte{9}}});
        const auto read = protobuf::decode_detailed(encoded_form(sent));
        expect(read.has_value() && read->type_url.has_value());
        if (read && read->type_url) {
          expect(*read->type_url == "type.googleapis.com/pkg.Thing"sv);
          expect(read->event == sent);
        }
      };
};

int main() {}
