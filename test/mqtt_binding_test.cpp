#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <boost/ut.hpp>

#include <cloudevents/binding/mqtt.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "binding_decode_options.hpp"
#include "codecs_under_test.hpp"
#include "equality.hpp"

// Every assertion that touches a codec is a function template instantiated for each enabled codec,
// as in the Kafka suite. A PUBLISH is a message: the payload is the body, the Content Type property
// is the header content-type and the User Properties are the other headers.

namespace {

using namespace std::string_view_literals;

using namespace ce::literals;

constexpr auto structured_media_type = "application/cloudevents+json"sv;
constexpr auto content_type_name = "content-type"sv;
constexpr auto minimal_document = R"({"specversion":"1.0","id":"1","source":"/s","type":"t"})"sv;

[[nodiscard]] auto base_event(ce::event::options rest = {}) -> ce::event {
  return ce::event{"1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

[[nodiscard]] auto structured_message(std::string_view document, const ce::raw_headers& more = {})
    -> ce::message {
  ce::message out{
      .header_fields = {{std::string{content_type_name}, std::string{structured_media_type}}},
      .body = ce::to_bytes(document)};
  for (const auto& [name, value] : more) {
    out.header_fields.add(name, value);
  }
  return out;
}

template<class Codec>
void check_structured_mode(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event({.subject = "s"_subject});

  auto laid_out = ce::mqtt::to_message<Codec>(subject, ce::content_mode::structured);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  const std::string* declared = laid_out->header_fields.find_exact(content_type_name);
  expect(declared != nullptr) << codec;
  if (declared != nullptr) {
    expect(*declared == structured_media_type) << codec;
  }
  expect(laid_out->header_fields.size() == 1U) << codec << ": no User Property is written";

  auto read_back = ce::mqtt::from_message<Codec>(*laid_out);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->id() == subject.id()) << codec;
    expect(bool{read_back->source() == subject.source()}) << codec;
    expect(read_back->type() == subject.type()) << codec;
    expect(ce_test::equal(read_back->subject(), subject.subject())) << codec;
  }
}

template<class Codec>
void check_user_properties_are_ignored(std::string_view codec) {
  using namespace boost::ut;

  const ce::message with_properties = structured_message(minimal_document,
                                                         {{"x-request-id", "abc"},
                                                          {"x-request-id", "def"},
                                                          {"id", "from-a-user-property"},
                                                          {"specversion", "9.9"},
                                                          {"ce-type", "other"}});

  auto read = ce::mqtt::from_message<Codec>(with_properties);
  expect(bool{read}) << codec;
  if (read) {
    expect(read->id() == "1"_id) << codec << ": the payload is the event";
    expect(read->type() == "t"_type) << codec;
  }
}

template<class Codec>
void check_payload_round_trip(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event({.subject = "s"_subject});

  const auto payload = ce::mqtt::to_payload<Codec>(subject);
  expect(bool{payload}) << codec;
  if (!payload) {
    return;
  }
  const auto message_form = ce::mqtt::to_message<Codec>(subject, ce::content_mode::structured);
  expect(message_form && ce::to_bytes(*payload) == message_form->body)
      << codec << ": the payload is the body of the 5.0 message";

  auto read_back = ce::mqtt::from_payload<Codec>(*payload);
  expect(bool{read_back}) << codec;
  if (read_back) {
    expect(read_back->id() == subject.id()) << codec;
    expect(ce_test::equal(read_back->subject(), subject.subject())) << codec;
  }
}

template<class Codec>
void check_payload_ignores_headers(std::string_view codec) {
  using namespace boost::ut;

  const ce::raw_headers carried[] = {
      {},
      {{"content-type", "text/plain"}},
      {{"content-type", "application/cloudevents-batch+json"}},
      {{"content-type", "application/cloudevents+json"}, {"content-type", "text/plain"}},
      {{"specversion", "1.0"}, {"x-request-id", "abc"}},
  };
  for (const auto& headers : carried) {
    const ce::message incoming{.header_fields = headers, .body = ce::to_bytes(minimal_document)};
    auto read = ce::mqtt::from_payload<Codec>(incoming);
    expect(bool{read}) << codec;
    if (read) {
      expect(read->id() == "1"_id) << codec;
    }
  }
}

template<class Codec>
void check_content_type_property(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = base_event({.datacontenttype = "application/json"_mediatype,
                                        .data = ce::json_text{.raw = R"({"a":1})"}});
  auto laid_out = ce::mqtt::to_message<Codec>(subject, ce::content_mode::structured);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  expect(!laid_out->header_fields.contains_exact("datacontenttype"))
      << codec << ": datacontenttype is never a User Property";
  const std::string* declared = laid_out->header_fields.find_exact(content_type_name);
  expect(declared != nullptr && *declared == structured_media_type)
      << codec << ": the Content Type property holds the event format, not the data type";

  auto read_back = ce::mqtt::from_message<Codec>(*laid_out);
  expect(read_back && ce_test::equal(read_back->datacontenttype(), subject.datacontenttype()))
      << codec << ": datacontenttype travels in the document";
}

template<class Codec>
void check_parameters_on_the_media_type(std::string_view codec) {
  using namespace boost::ut;

  for (const auto media_type : {"application/cloudevents+json; charset=utf-8"sv,
                                "application/cloudevents+json;charset=utf-8"sv,
                                "application/cloudevents"sv}) {
    const ce::message incoming{
        .header_fields = {{std::string{content_type_name}, std::string{media_type}}},
        .body = ce::to_bytes(minimal_document)};
    expect(ce::mqtt::detect_content_mode(incoming) == ce::content_mode::structured)
        << codec << ": " << media_type;
    auto read = ce::mqtt::from_message<Codec>(incoming);
    expect(bool{read}) << codec << ": " << media_type;
  }
}

template<class Codec>
void check_not_a_cloudevent(std::string_view codec) {
  using namespace boost::ut;

  const ce::message plain_messages[] = {
      {},
      {.body = ce::to_bytes("hello")},
      {.header_fields = {{"content-type", "text/plain"}}, .body = ce::to_bytes("hello")},
      {.header_fields = {{"content-type", "application/json"}},
       .body = ce::to_bytes(minimal_document)},
      {.header_fields = {{"x-request-id", "abc"}}},
  };
  for (const auto& incoming : plain_messages) {
    const auto read = ce::mqtt::from_message<Codec>(incoming);
    expect(!read.has_value()) << codec;
    if (!read) {
      expect(read.error().code == ce::errc::not_a_cloudevent) << codec;
    }
  }
}

template<class Codec>
void check_malformed_payload(std::string_view codec) {
  using namespace boost::ut;

  const auto unparsable = ce::mqtt::from_message<Codec>(structured_message("{not json"));
  expect(!unparsable.has_value()) << codec;
  if (!unparsable) {
    expect(unparsable.error().code == ce::errc::parse_error) << codec;
  }

  const auto empty = ce::mqtt::from_message<Codec>(structured_message(""));
  expect(!empty.has_value()) << codec;
  if (!empty) {
    expect(empty.error().code != ce::errc::not_a_cloudevent) << codec;
  }

  const auto missing_id =
      ce::mqtt::from_message<Codec>(structured_message(R"({"specversion":"1.0","type":"t"})"));
  expect(!missing_id.has_value()) << codec;
  if (!missing_id) {
    expect(missing_id.error().code != ce::errc::not_a_cloudevent) << codec;
  }

  const auto bad_payload = ce::mqtt::from_payload<Codec>("{not json"sv);
  expect(!bad_payload.has_value()) << codec;
  if (!bad_payload) {
    expect(bad_payload.error().code == ce::errc::parse_error) << codec;
  }
}

template<class Codec>
void check_batch_refused(std::string_view codec) {
  using namespace boost::ut;

  const auto written = ce::mqtt::to_message<Codec>(base_event(), ce::content_mode::batched);
  expect(!written.has_value()) << codec;
  if (!written) {
    expect(written.error().code == ce::errc::invalid_argument) << codec;
  }

  for (const auto media_type :
       {"application/cloudevents-batch+json"sv, "application/cloudevents-batch"sv}) {
    const ce::message incoming{
        .header_fields = {{std::string{content_type_name}, std::string{media_type}}},
        .body = ce::to_bytes(minimal_document)};
    expect(ce::mqtt::detect_content_mode(incoming) == ce::content_mode::batched) << codec;
    const auto read = ce::mqtt::from_message<Codec>(incoming);
    expect(!read.has_value()) << codec << ": " << media_type;
    if (!read) {
      expect(read.error().code == ce::errc::invalid_argument) << codec << ": " << media_type;
    }
  }
}

template<class Codec>
void check_binary_mode_is_not_written(std::string_view codec) {
  using namespace boost::ut;

  const auto written = ce::mqtt::to_message<Codec>(base_event(), ce::content_mode::binary_mode);
  expect(!written.has_value()) << codec;
  if (!written) {
    expect(written.error().code == ce::errc::invalid_argument) << codec;
  }
}

template<class Codec>
void check_names_are_byte_exact(std::string_view codec) {
  using namespace boost::ut;

  for (const auto name :
       {"Content-Type"sv, "CONTENT-TYPE"sv, "content-type "sv, "Content_Type"sv}) {
    const ce::message incoming{
        .header_fields = {{std::string{name}, std::string{structured_media_type}}},
        .body = ce::to_bytes(minimal_document)};
    expect(ce::mqtt::detect_content_mode(incoming) == ce::content_mode::binary_mode)
        << codec << ": " << name;
    const auto read = ce::mqtt::from_message<Codec>(incoming);
    expect(!read.has_value()) << codec << ": " << name;
    if (!read) {
      expect(read.error().code == ce::errc::not_a_cloudevent) << codec << ": " << name;
    }
  }

  // A User Property that differs only in case is another name, so it is not a second
  // Content Type property.
  const auto beside = ce::mqtt::from_message<Codec>(
      structured_message(minimal_document, {{"Content-Type", "text/plain"}}));
  expect(bool{beside}) << codec << ": Content-Type beside content-type is another name";
}

template<class Codec>
void check_user_property_named_content_type(std::string_view codec) {
  using namespace boost::ut;

  const ce::raw_headers doubled[] = {
      {{"content-type", "application/cloudevents+json"}, {"content-type", "text/plain"}},
      {{"content-type", "text/plain"}, {"content-type", "application/cloudevents+json"}},
      {{"content-type", "application/cloudevents+json"},
       {"content-type", "application/cloudevents+json"}},
      {{"content-type", "text/plain"}, {"content-type", "text/plain"}},
      {{"content-type", "application/cloudevents-batch+json"}, {"content-type", "text/plain"}},
  };
  for (const auto& headers : doubled) {
    const ce::message incoming{.header_fields = headers, .body = ce::to_bytes(minimal_document)};
    const auto read = ce::mqtt::from_message<Codec>(incoming);
    expect(!read.has_value()) << codec;
    if (!read) {
      expect(read.error().code == ce::errc::invalid_argument) << codec;
    }
  }

  const auto single = ce::mqtt::from_message<Codec>(structured_message(minimal_document));
  expect(bool{single}) << codec << ": one header of the name is the Content Type property";
}

template<class Codec>
void check_decode_options(std::string_view codec) {
  using namespace boost::ut;

  ce_test::check_structured_ignores_parse_binary_body<Codec>(
      codec,
      [](const ce::event& subject, ce::content_mode mode) {
        return ce::mqtt::to_message<Codec>(subject, mode);
      },
      [](const ce::message& laid_out, ce::json::decode_options options) {
        return ce::mqtt::from_message<Codec>(laid_out, options);
      });

  const auto laid_out =
      ce::mqtt::to_message<Codec>(ce_test::options_event(), ce::content_mode::structured);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  const std::size_t length = laid_out->body.size();

  const auto at_limit = ce::mqtt::from_message<Codec>(
      *laid_out, ce::json::decode_options{.retain_document_up_to = length});
  expect(at_limit && std::holds_alternative<ce::json_document>(at_limit->data())) << codec;

  const auto over_limit = ce::mqtt::from_message<Codec>(
      *laid_out, ce::json::decode_options{.retain_document_up_to = length - 1U});
  expect(over_limit && std::holds_alternative<ce::json_text>(over_limit->data())) << codec;

  const auto by_payload =
      ce::mqtt::from_payload<Codec>(*ce::mqtt::to_payload<Codec>(ce_test::options_event()),
                                    ce::json::decode_options{.retain_document_up_to = 0});
  expect(by_payload && std::holds_alternative<ce::json_text>(by_payload->data())) << codec;
}

template<class Check>
void for_each_codec_named(Check check) {
  ce_test::for_each_codec([&check]<class C>(std::string_view codec) {
    boost::ut::test(std::string{codec}) = [&check, codec] { check.template operator()<C>(codec); };
  });
}

}  // namespace

// spec: SWR-MQTT-0001
const boost::ut::suite<"mqtt-structured-mode-5-0"> mqtt_structured_mode = [] {
  for_each_codec_named([]<class C>(std::string_view codec) { check_structured_mode<C>(codec); });
};

// spec: SWR-MQTT-0001
const boost::ut::suite<"mqtt-structured-ignores-user-properties"> mqtt_ignores_properties = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_user_properties_are_ignored<C>(codec); });
};

// spec: SWR-MQTT-0001
const boost::ut::suite<"mqtt-writes-structured-mode-only"> mqtt_structured_only = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_binary_mode_is_not_written<C>(codec); });
};

// spec: SWR-MQTT-0001
const boost::ut::suite<"mqtt-from-message-takes-decode-options"> mqtt_decode_options = [] {
  for_each_codec_named([]<class C>(std::string_view codec) { check_decode_options<C>(codec); });
};

// spec: SWR-MQTT-0002
const boost::ut::suite<"mqtt-3-1-1-payload-round-trip"> mqtt_payload_round_trip = [] {
  for_each_codec_named([]<class C>(std::string_view codec) { check_payload_round_trip<C>(codec); });
};

// spec: SWR-MQTT-0002
const boost::ut::suite<"mqtt-3-1-1-ignores-headers"> mqtt_payload_ignores_headers = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_payload_ignores_headers<C>(codec); });
};

// spec: SWR-MQTT-0003
const boost::ut::suite<"mqtt-content-type-property"> mqtt_content_type_property = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_content_type_property<C>(codec); });
};

// spec: SWR-MQTT-0004
const boost::ut::suite<"mqtt-mode-detection"> mqtt_mode_detection = [] {
  using namespace boost::ut;

  "no Content Type property is not structured"_test = [] {
    expect(ce::mqtt::detect_content_mode(ce::message{}) == ce::content_mode::binary_mode);
  };
  "a CloudEvents Content Type is structured"_test = [] {
    const ce::message incoming{
        .header_fields = {{std::string{content_type_name}, std::string{structured_media_type}}}};
    expect(ce::mqtt::detect_content_mode(incoming) == ce::content_mode::structured);
  };
  "another Content Type is not structured"_test = [] {
    for (const auto media_type : {"application/json"sv,
                                  "text/plain"sv,
                                  "application/cloudeven"sv,
                                  "x-application/cloudevents+json"sv,
                                  ""sv}) {
      const ce::message incoming{
          .header_fields = {{std::string{content_type_name}, std::string{media_type}}}};
      expect(ce::mqtt::detect_content_mode(incoming) == ce::content_mode::binary_mode)
          << media_type;
    }
  };
  "a User Property does not decide the mode"_test = [] {
    const ce::message incoming{.header_fields = {{"specversion", "1.0"}}};
    expect(ce::mqtt::detect_content_mode(incoming) == ce::content_mode::binary_mode);
  };
};

// spec: SWR-MQTT-0004
const boost::ut::suite<"mqtt-content-type-with-parameters"> mqtt_parameters = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_parameters_on_the_media_type<C>(codec); });
};

// spec: SWR-MQTT-0005
const boost::ut::suite<"mqtt-not-a-cloudevent"> mqtt_not_a_cloudevent = [] {
  for_each_codec_named([]<class C>(std::string_view codec) { check_not_a_cloudevent<C>(codec); });
};

// spec: SWR-MQTT-0005
const boost::ut::suite<"mqtt-malformed-structured-payload"> mqtt_malformed = [] {
  for_each_codec_named([]<class C>(std::string_view codec) { check_malformed_payload<C>(codec); });
};

// spec: SWR-MQTT-0006
const boost::ut::suite<"mqtt-refuses-batch"> mqtt_refuses_batch = [] {
  for_each_codec_named([]<class C>(std::string_view codec) { check_batch_refused<C>(codec); });
};

// spec: SWR-MQTT-0007
const boost::ut::suite<"mqtt-names-are-byte-exact"> mqtt_byte_exact = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_names_are_byte_exact<C>(codec); });
};

// spec: SWR-MQTT-0008
const boost::ut::suite<"mqtt-user-property-named-content-type"> mqtt_user_property_content_type =
    [] {
      for_each_codec_named([]<class C>(std::string_view codec) {
        check_user_property_named_content_type<C>(codec);
      });
    };

int main() {}
