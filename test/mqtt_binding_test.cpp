#include <cstddef>
#include <cstdint>
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

using unprefixed = ce::mqtt::unprefixed_names;
using prefixed = ce::mqtt::prefixed_names;

constexpr auto go_prefix = "ce-"sv;

[[nodiscard]] auto rich_event() -> ce::event {
  const auto when = ce::parse_timestamp("2026-09-21T00:00:00Z");
  return base_event({.datacontenttype = "text/plain"_mediatype,
                     .dataschema = "https://example.test/schema"_dataschema,
                     .subject = "s"_subject,
                     .time = *when,
                     .extensions = {{"alpha"_ext, std::string{"text"}},
                                    {"beta"_ext, std::int32_t{42}},
                                    {"gamma"_ext, true},
                                    {"delta"_ext, ce::binary{std::byte{0}, std::byte{255}}}},
                     .data = std::string{"hello"}});
}

[[nodiscard]] auto binary_message(const ce::raw_headers& properties, std::string_view body = {})
    -> ce::message {
  return ce::message{.header_fields = properties, .body = ce::to_bytes(body)};
}

template<class Codec, class Names>
void check_binary_round_trip(std::string_view codec) {
  using namespace boost::ut;

  const ce::event subject = rich_event();
  auto laid_out = ce::mqtt::to_message<Codec, Names>(subject, ce::content_mode::binary_mode);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  expect(laid_out->body == ce::to_bytes("hello"sv)) << codec << ": the data is the body as is";
  const std::string* declared = laid_out->header_fields.find_exact(content_type_name);
  expect(declared != nullptr && *declared == "text/plain")
      << codec << ": datacontenttype is the Content Type property";
  expect(!laid_out->header_fields.contains_exact(
      std::string{Names::attribute_prefix}.append("datacontenttype")))
      << codec << ": and never a User Property";
  expect(ce::mqtt::detect_content_mode(*laid_out) == ce::content_mode::binary_mode) << codec;

  auto read_back = ce::mqtt::from_message<Codec, Names>(*laid_out);
  expect(bool{read_back}) << codec;
  if (!read_back) {
    return;
  }
  expect(read_back->id() == subject.id()) << codec;
  expect(bool{read_back->source() == subject.source()}) << codec;
  expect(read_back->type() == subject.type()) << codec;
  expect(ce_test::equal(read_back->subject(), subject.subject())) << codec;
  expect(ce_test::equal(read_back->time(), subject.time())) << codec;
  expect(ce_test::equal(read_back->dataschema(), subject.dataschema())) << codec;
  expect(ce_test::equal(read_back->datacontenttype(), subject.datacontenttype())) << codec;
  const auto* bytes = std::get_if<ce::binary>(&read_back->data());
  expect(bytes != nullptr && *bytes == ce::to_bytes("hello"sv)) << codec;
}

template<class Codec>
void check_unprefixed_names(std::string_view codec) {
  using namespace boost::ut;

  auto laid_out = ce::mqtt::to_message<Codec>(rich_event(), ce::content_mode::binary_mode);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  for (const auto name : {"specversion"sv,
                          "id"sv,
                          "source"sv,
                          "type"sv,
                          "subject"sv,
                          "time"sv,
                          "dataschema"sv,
                          "alpha"sv,
                          "beta"sv,
                          "gamma"sv,
                          "delta"sv}) {
    expect(laid_out->header_fields.contains_exact(name)) << codec << ": " << name;
  }
  const std::string* version = laid_out->header_fields.find_exact("specversion");
  expect(version != nullptr && *version == "1.0") << codec;
  expect(laid_out->header_fields.size() == 12U)
      << codec << ": eleven User Properties and the Content Type property";
}

template<class Codec>
void check_no_prefix_by_default(std::string_view codec) {
  using namespace boost::ut;

  auto laid_out = ce::mqtt::to_message<Codec>(rich_event(), ce::content_mode::binary_mode);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  for (const auto& [name, value] : laid_out->header_fields) {
    expect(!std::string_view{name}.starts_with(go_prefix)) << codec << ": " << name;
  }
}

template<class Codec>
void check_prefixed_write(std::string_view codec) {
  using namespace boost::ut;

  auto laid_out =
      ce::mqtt::to_message<Codec, prefixed>(rich_event(), ce::content_mode::binary_mode);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  for (const auto& [name, value] : laid_out->header_fields) {
    expect(name == content_type_name || std::string_view{name}.starts_with(go_prefix))
        << codec << ": " << name;
  }
  for (const auto name : {"ce-specversion"sv,
                          "ce-id"sv,
                          "ce-source"sv,
                          "ce-type"sv,
                          "ce-subject"sv,
                          "ce-time"sv,
                          "ce-dataschema"sv,
                          "ce-alpha"sv}) {
    expect(laid_out->header_fields.contains_exact(name)) << codec << ": " << name;
  }
  expect(!laid_out->header_fields.contains_exact("specversion")) << codec;
  expect(laid_out->header_fields.contains_exact(content_type_name))
      << codec << ": the Content Type property keeps its name";

  auto structured =
      ce::mqtt::to_message<Codec, prefixed>(rich_event(), ce::content_mode::structured);
  expect(structured && structured->header_fields.size() == 1U)
      << codec << ": structured mode writes no User Property under either policy";
}

template<class Codec>
void check_prefixed_read(std::string_view codec) {
  using namespace boost::ut;

  const ce::message go_shaped = binary_message({{"ce-specversion", "1.0"},
                                                {"ce-id", "7"},
                                                {"ce-source", "/s"},
                                                {"ce-type", "t"},
                                                {"ce-alpha", "x"},
                                                {"specversion", "9.9"},
                                                {"id", "ignored"},
                                                {"CE-id", "ignored"},
                                                {"ce-Upper", "ignored"},
                                                {"ce-", "ignored"},
                                                {"ce-x-y", "ignored"}});
  auto read = ce::mqtt::from_message<Codec, prefixed>(go_shaped);
  expect(bool{read}) << codec;
  if (read) {
    expect(read->id() == "7"_id) << codec;
    expect(read->extensions().size() == 1U) << codec << ": only ce-alpha";
  }
}

template<class Codec>
void check_default_names_are_the_text(std::string_view codec) {
  using namespace boost::ut;

  const ce::message text_shaped =
      binary_message({{"specversion", "1.0"}, {"id", "7"}, {"source", "/s"}, {"type", "t"}});
  auto by_default = ce::mqtt::from_message<Codec>(text_shaped);
  expect(bool{by_default}) << codec;
  auto explicit_default = ce::mqtt::from_message<Codec, unprefixed>(text_shaped);
  expect(bool{explicit_default}) << codec;
  const auto under_prefix = ce::mqtt::from_message<Codec, prefixed>(text_shaped);
  expect(!under_prefix.has_value() && under_prefix.error().code == ce::errc::not_a_cloudevent)
      << codec << ": the text's names are not the prefixed form";

  const ce::message go_shaped = binary_message(
      {{"ce-specversion", "1.0"}, {"ce-id", "7"}, {"ce-source", "/s"}, {"ce-type", "t"}});
  const auto refused = ce::mqtt::from_message<Codec>(go_shaped);
  expect(!refused.has_value() && refused.error().code == ce::errc::not_a_cloudevent)
      << codec << ": the Go form is not the default";
}

template<class Codec>
void check_prefixed_datacontenttype_refused(std::string_view codec) {
  using namespace boost::ut;

  const auto read =
      ce::mqtt::from_message<Codec, prefixed>(binary_message({{"ce-specversion", "1.0"},
                                                              {"ce-id", "7"},
                                                              {"ce-source", "/s"},
                                                              {"ce-type", "t"},
                                                              {"ce-datacontenttype", "text/plain"},
                                                              {"content-type", "text/plain"}}));
  expect(!read.has_value() && read.error().code == ce::errc::invalid_argument)
      << codec << ": ce-datacontenttype is refused even when it equals the Content Type";
}

template<class Codec>
void check_binary_extensions(std::string_view codec) {
  using namespace boost::ut;

  const ce::message incoming = binary_message({{"specversion", "1.0"},
                                               {"id", "7"},
                                               {"source", "/s"},
                                               {"type", "t"},
                                               {"alpha", "text"},
                                               {"beta", "42"},
                                               {"gamma", "true"}});
  auto read = ce::mqtt::from_message<Codec>(incoming);
  expect(bool{read}) << codec;
  if (!read) {
    return;
  }
  expect(read->extensions().size() == 3U) << codec;
  for (const auto& [name, value] : read->extensions()) {
    expect(std::holds_alternative<std::string>(value)) << codec << ": " << name.view();
  }
  const auto beta = read->extensions().find("beta"sv);
  expect(beta != read->extensions().end() && std::get<std::string>(beta->second) == "42")
      << codec << ": a binary-mode extension is a string";
}

template<class Codec>
void check_duplicate_datacontenttype(std::string_view codec) {
  using namespace boost::ut;

  const ce::raw_headers base = {
      {"specversion", "1.0"}, {"id", "7"}, {"source", "/s"}, {"type", "t"}};
  const auto with = [&base](const ce::raw_headers& more) {
    ce::raw_headers all = base;
    for (const auto& [name, value] : more) {
      all.add(name, value);
    }
    return binary_message(all, "{}");
  };

  const auto equal_copy = ce::mqtt::from_message<Codec>(
      with({{"content-type", "application/json"}, {"datacontenttype", "application/json"}}));
  expect(bool{equal_copy}) << codec << ": example 3.1.4 of the text";
  if (equal_copy) {
    expect(
        ce_test::equal(equal_copy->datacontenttype(), std::optional{"application/json"_mediatype}))
        << codec;
  }

  const ce::raw_headers refused[] = {
      {{"content-type", "application/json"}, {"datacontenttype", "text/plain"}},
      {{"datacontenttype", "application/json"}},
      {{"content-type", "application/json"},
       {"datacontenttype", "application/json"},
       {"datacontenttype", "application/json"}},
  };
  for (const auto& more : refused) {
    const auto read = ce::mqtt::from_message<Codec>(with(more));
    expect(!read.has_value() && read.error().code == ce::errc::invalid_argument) << codec;
  }
}

template<class Codec, class Names>
void check_repeated_attribute_refused(std::string_view codec) {
  using namespace boost::ut;

  const std::string pre{Names::attribute_prefix};
  for (const auto repeated : {"id"sv, "type"sv, "alpha"sv, "specversion"sv}) {
    ce::message incoming = binary_message({{pre + "specversion", "1.0"},
                                           {pre + "id", "7"},
                                           {pre + "source", "/s"},
                                           {pre + "type", "t"},
                                           {pre + "alpha", "a"}});
    incoming.header_fields.add(pre + std::string{repeated}, "again");
    const auto read = ce::mqtt::from_message<Codec, Names>(incoming);
    expect(!read.has_value() && read.error().code == ce::errc::invalid_argument)
        << codec << ": " << repeated;
  }
}

template<class Codec>
void check_values_canonical(std::string_view codec) {
  using namespace boost::ut;

  auto laid_out = ce::mqtt::to_message<Codec>(rich_event(), ce::content_mode::binary_mode);
  expect(bool{laid_out}) << codec;
  if (!laid_out) {
    return;
  }
  const auto value_of = [&laid_out](std::string_view name) {
    const std::string* found = laid_out->header_fields.find_exact(name);
    return found != nullptr ? *found : std::string{"<missing>"};
  };
  expect(value_of("time") == "2026-09-21T00:00:00Z") << codec;
  expect(value_of("beta") == "42") << codec;
  expect(value_of("gamma") == "true") << codec;
  expect(value_of("delta") == "AP8=") << codec << ": binary is base64";
  expect(value_of("dataschema") == "https://example.test/schema") << codec;
}

constexpr std::size_t max_value_bytes = 65535U;

template<class Codec, class Names>
void check_value_utf8_nul_and_length(std::string_view codec) {
  using namespace boost::ut;

  const auto written = [](std::string text) {
    return ce::mqtt::to_message<Codec, Names>(
        base_event({.extensions = {{"alpha"_ext, std::move(text)}}}),
        ce::content_mode::binary_mode);
  };

  const auto invalid = written("\xC3\x28");
  expect(!invalid.has_value() && invalid.error().code == ce::errc::invalid_utf8) << codec;
  const auto surrogate = written("\xED\xA0\x80");
  expect(!surrogate.has_value() && surrogate.error().code == ce::errc::invalid_utf8) << codec;
  const auto nul = written(std::string{"a\0b", 3});
  expect(!nul.has_value() && nul.error().code == ce::errc::invalid_argument) << codec;
  const auto at_limit = written(std::string(max_value_bytes, 'a'));
  expect(bool{at_limit}) << codec << ": 65535 bytes fit a UTF-8 Encoded String";
  const auto over_limit = written(std::string(max_value_bytes + 1U, 'a'));
  expect(!over_limit.has_value() && over_limit.error().code == ce::errc::invalid_argument) << codec;
  expect(bool{written("\xF0\x9F\x98\x80")}) << codec << ": valid UTF-8 is written as is";

  const std::string pre{Names::attribute_prefix};
  const auto read_with = [&pre](std::string text) {
    return ce::mqtt::from_message<Codec, Names>(binary_message({{pre + "specversion", "1.0"},
                                                                {pre + "id", "7"},
                                                                {pre + "source", "/s"},
                                                                {pre + "type", "t"},
                                                                {pre + "alpha", std::move(text)}}));
  };
  const auto read_invalid = read_with("\xC3\x28");
  expect(!read_invalid.has_value() && read_invalid.error().code == ce::errc::invalid_utf8) << codec;
  const auto read_nul = read_with(std::string{"a\0b", 3});
  expect(!read_nul.has_value() && read_nul.error().code == ce::errc::invalid_argument) << codec;
  const auto read_long = read_with(std::string(max_value_bytes + 1U, 'a'));
  expect(!read_long.has_value() && read_long.error().code == ce::errc::invalid_argument) << codec;
  expect(bool{read_with(std::string(max_value_bytes, 'a'))}) << codec;
  expect(bool{read_with("\xF0\x9F\x98\x80")}) << codec;
}

template<class Codec>
void check_foreign_property_ignored(std::string_view codec) {
  using namespace boost::ut;

  const auto read = ce::mqtt::from_message<Codec>(binary_message({{"specversion", "1.0"},
                                                                  {"x-request-id", "abc"},
                                                                  {"id", "7"},
                                                                  {"x-request-id", "def"},
                                                                  {"source", "/s"},
                                                                  {"Trace_Id", "q"},
                                                                  {"type", "t"},
                                                                  {"ce-type", "other"},
                                                                  {"UPPER", "q"},
                                                                  {"", "empty"},
                                                                  {"a b", "q"}}));
  expect(bool{read}) << codec << ": a foreign User Property is not fatal";
  if (read) {
    expect(read->id() == "7"_id) << codec;
    expect(read->type() == "t"_type) << codec;
    expect(read->extensions().empty()) << codec << ": none of them is an extension";
  }

  const auto only_foreign =
      ce::mqtt::from_message<Codec>(binary_message({{"x-request-id", "abc"}}));
  expect(!only_foreign.has_value() && only_foreign.error().code == ce::errc::not_a_cloudevent)
      << codec;
}

template<class Codec>
void check_binary_malformed(std::string_view codec) {
  using namespace boost::ut;

  const auto missing_type = ce::mqtt::from_message<Codec>(
      binary_message({{"specversion", "1.0"}, {"id", "7"}, {"source", "/s"}}));
  expect(!missing_type.has_value() && missing_type.error().code != ce::errc::not_a_cloudevent)
      << codec;
  const auto bad_version = ce::mqtt::from_message<Codec>(
      binary_message({{"specversion", "2.0"}, {"id", "7"}, {"source", "/s"}, {"type", "t"}}));
  expect(!bad_version.has_value() && bad_version.error().code == ce::errc::unsupported_spec_version)
      << codec;
  const auto bad_time = ce::mqtt::from_message<Codec>(binary_message(
      {{"specversion", "1.0"}, {"id", "7"}, {"source", "/s"}, {"type", "t"}, {"time", "soon"}}));
  expect(!bad_time.has_value()) << codec;
  const auto bad_media_type =
      ce::mqtt::from_message<Codec>(binary_message({{"specversion", "1.0"},
                                                    {"id", "7"},
                                                    {"source", "/s"},
                                                    {"type", "t"},
                                                    {"content-type", "not a media type"}}));
  expect(!bad_media_type.has_value()) << codec;
}

template<class Codec>
void check_binary_json_body(std::string_view codec) {
  using namespace boost::ut;

  const auto read =
      ce::mqtt::from_message<Codec>(binary_message({{"specversion", "1.0"},
                                                    {"id", "7"},
                                                    {"source", "/s"},
                                                    {"type", "t"},
                                                    {"content-type", "application/json"}},
                                                   R"({"a":1})"));
  expect(bool{read}) << codec;
  if (read) {
    expect(std::holds_alternative<ce::json_document>(read->data())) << codec;
  }
}

template<class Codec>
void check_binary_mode_detection(std::string_view codec) {
  using namespace boost::ut;

  // A Content Type that is not a CloudEvents one, with the version User Property, is binary.
  const auto binary = ce::mqtt::from_message<Codec>(binary_message({{"content-type", "text/plain"},
                                                                    {"specversion", "1.0"},
                                                                    {"id", "7"},
                                                                    {"source", "/s"},
                                                                    {"type", "t"}},
                                                                   "x"));
  expect(bool{binary}) << codec;
  // A CloudEvents Content Type wins over the version User Property.
  const auto structured = ce::mqtt::from_message<Codec>(
      structured_message(minimal_document, {{"specversion", "1.0"}, {"id", "from-property"}}));
  expect(structured && structured->id() == "1"_id) << codec;
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

  // The attribute names are byte for byte too, under both policies.
  for (const auto version : {"Specversion"sv, "SPECVERSION"sv, "specversion "sv}) {
    const auto unprefixed_read = ce::mqtt::from_message<Codec>(binary_message(
        {{std::string{version}, "1.0"}, {"id", "7"}, {"source", "/s"}, {"type", "t"}}));
    expect(!unprefixed_read.has_value() &&
           unprefixed_read.error().code == ce::errc::not_a_cloudevent)
        << codec << ": " << version;
  }
  for (const auto version : {"CE-specversion"sv, "Ce-specversion"sv, "ce-Specversion"sv}) {
    const auto prefixed_read = ce::mqtt::from_message<Codec, prefixed>(binary_message(
        {{std::string{version}, "1.0"}, {"ce-id", "7"}, {"ce-source", "/s"}, {"ce-type", "t"}}));
    expect(!prefixed_read.has_value() && prefixed_read.error().code == ce::errc::not_a_cloudevent)
        << codec << ": " << version;
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
  for_each_codec_named([]<class C>(std::string_view codec) {
    using namespace boost::ut;
    const auto structured = ce::mqtt::to_message<C>(base_event(), ce::content_mode::structured);
    expect(structured && structured->header_fields.size() == 1U) << codec;
  });
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

// spec: SWR-MQTT-0009
const boost::ut::suite<"mqtt-binary-mode-unprefixed-names"> mqtt_binary_unprefixed = [] {
  for_each_codec_named([]<class C>(std::string_view codec) {
    check_binary_round_trip<C, unprefixed>(codec);
    check_unprefixed_names<C>(codec);
  });
};

// spec: SWR-MQTT-0009
const boost::ut::suite<"mqtt-binary-mode-no-ce-prefix"> mqtt_binary_no_prefix = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_no_prefix_by_default<C>(codec); });
};

// spec: SWR-MQTT-0010
const boost::ut::suite<"mqtt-binary-extensions"> mqtt_binary_extensions = [] {
  for_each_codec_named([]<class C>(std::string_view codec) {
    check_binary_extensions<C>(codec);
    check_binary_json_body<C>(codec);
    check_binary_malformed<C>(codec);
  });
};

// spec: SWR-MQTT-0010
const boost::ut::suite<"mqtt-duplicate-datacontenttype"> mqtt_duplicate_media_type = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_duplicate_datacontenttype<C>(codec); });
};

// spec: SWR-MQTT-0010
const boost::ut::suite<"mqtt-repeated-attribute-refused"> mqtt_repeated_attribute = [] {
  for_each_codec_named([]<class C>(std::string_view codec) {
    check_repeated_attribute_refused<C, unprefixed>(codec);
    check_repeated_attribute_refused<C, prefixed>(codec);
  });
};

// spec: SWR-MQTT-0011
const boost::ut::suite<"mqtt-values-canonical"> mqtt_values_canonical = [] {
  for_each_codec_named([]<class C>(std::string_view codec) { check_values_canonical<C>(codec); });
};

// spec: SWR-MQTT-0011
const boost::ut::suite<"mqtt-value-utf8-and-nul"> mqtt_value_text = [] {
  for_each_codec_named([]<class C>(std::string_view codec) {
    check_value_utf8_nul_and_length<C, unprefixed>(codec);
    check_value_utf8_nul_and_length<C, prefixed>(codec);
  });
};

// spec: SWR-MQTT-0012
const boost::ut::suite<"mqtt-prefixed-names-write"> mqtt_prefixed_write = [] {
  for_each_codec_named([]<class C>(std::string_view codec) {
    check_prefixed_write<C>(codec);
    check_binary_round_trip<C, prefixed>(codec);
  });
};

// spec: SWR-MQTT-0012
const boost::ut::suite<"mqtt-prefixed-names-read"> mqtt_prefixed_read = [] {
  for_each_codec_named([]<class C>(std::string_view codec) { check_prefixed_read<C>(codec); });
};

// spec: SWR-MQTT-0012
const boost::ut::suite<"mqtt-default-names-are-the-text"> mqtt_default_names = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_default_names_are_the_text<C>(codec); });
};

// spec: SWR-MQTT-0012
const boost::ut::suite<"mqtt-prefixed-datacontenttype-refused"> mqtt_prefixed_media_type = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_prefixed_datacontenttype_refused<C>(codec); });
};

// spec: SWR-MQTT-0013
const boost::ut::suite<"mqtt-foreign-user-property-ignored"> mqtt_foreign_ignored = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_foreign_property_ignored<C>(codec); });
};

// spec: SWR-MQTT-0004
const boost::ut::suite<"mqtt-binary-mode-detection"> mqtt_binary_detection = [] {
  for_each_codec_named(
      []<class C>(std::string_view codec) { check_binary_mode_detection<C>(codec); });
};

int main() {}
