#include <algorithm>
#include <cstddef>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <boost/ut.hpp>

#include <nlohmann/json.hpp>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/binding/mqtt.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "codecs_under_test.hpp"
#include "corpus.hpp"
#include "equality.hpp"
#include "fixtures_path.hpp"
#include "payload.hpp"

// MQTT 5.0 PUBLISH messages recorded from the Go CloudEvents SDK, and the messages this SDK
// produced for it to read (SWR-MQTT-0014, STK-INTEROP-0001).
//
// The Go SDK converts an event to and from an in-memory paho.Publish, which is the topic, the
// payload and the properties a broker would carry, so no broker is involved and the MQTT packet
// bytes are not under test. This suite proves one direction: the Go messages decode here to the
// event Go encoded. The other direction is proven by interop/run.sh, which feeds
// test/fixtures/interop/mqtt/cpp to the Go SDK and fails if it decodes a message to a different
// event.
//
// Binary mode is where the SDKs and the binding text disagree (SWR-MQTT-0015): Go writes and
// requires the names with a ce- prefix. The Go binary goldens decode under `prefixed_names`, the
// binary messages this SDK wrote under that policy are the ones Go decodes, and under the default
// (the text's names) each Go binary golden is not_a_cloudevent. The recorded messages are not
// edited to hide the difference.

namespace {

using namespace std::string_view_literals;

constexpr auto go_producer = "go"sv;
constexpr auto cpp_producer = "cpp"sv;
constexpr auto structured_media_type = "application/cloudevents+json"sv;
constexpr auto content_type_name = "content-type"sv;
constexpr auto structured_mode = "structured"sv;
constexpr auto binary_mode = "binary"sv;
constexpr auto go_prefix = "ce-"sv;

/// One PUBLISH as an SDK wrote it.
struct recorded {
  ce::message on_the_wire = {};
  std::optional<std::string> content_type = std::nullopt;
  std::size_t user_properties = 0;
};

[[nodiscard]] auto read_file(const std::string& path) -> std::string {
  std::ifstream in{path, std::ios::binary};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

[[nodiscard]] auto read_message(std::string_view producer,
                                std::string_view name,
                                std::string_view mode = structured_mode)
    -> std::optional<recorded> {
  const std::string raw =
      read_file(ce_fixtures::path("interop/mqtt/" + std::string{producer} + "/" +
                                  std::string{name} + "." + std::string{mode} + ".json"));
  const auto document = nlohmann::json::parse(raw, nullptr, false, false);
  if (document.is_discarded()) {
    return std::nullopt;
  }
  auto payload = ce::base64_decode(document["payload_base64"].get<std::string>());
  if (!payload) {
    return std::nullopt;
  }
  recorded out;
  out.on_the_wire.body = std::move(*payload);
  if (!document["content_type"].is_null()) {
    out.content_type = document["content_type"].get<std::string>();
    out.on_the_wire.header_fields.add(std::string{content_type_name}, *out.content_type);
  }
  for (const auto& property : document["user_properties"]) {
    out.on_the_wire.header_fields.add(property[0].get<std::string>(),
                                      property[1].get<std::string>());
    ++out.user_properties;
  }
  return out;
}

[[nodiscard]] auto rendered_extensions(const ce::event& subject)
    -> std::map<std::string, std::string> {
  std::map<std::string, std::string> out;
  for (const auto& [name, value] : subject.extensions()) {
    out.emplace(std::string{name.view()}, ce::binding::render_attribute(value));
  }
  return out;
}

/// The payload of `expected` against what was decoded. A JSON payload is compared as a value
/// because the format may spell it differently, anything else exactly (D-INTEROP-1).
template<class C>
[[nodiscard]] auto same_payload(const ce::event& decoded, const ce::event& expected) -> bool {
  if (const auto* json = std::get_if<ce::json_text>(&expected.data())) {
    return ce_test::same_json_payload<C>(decoded.data(), json->raw);
  }
  return decoded.data() == expected.data();
}

template<class C>
void check_same_event(std::string_view where, const ce::event& decoded, const ce::event& expected) {
  using namespace boost::ut;
  expect(decoded.id() == expected.id()) << where;
  expect(bool{decoded.source() == expected.source()}) << where;
  expect(decoded.type() == expected.type()) << where;
  expect(ce_test::equal(decoded.subject(), expected.subject())) << where;
  expect(ce_test::equal(decoded.time(), expected.time())) << where;
  expect(ce_test::equal(decoded.dataschema(), expected.dataschema())) << where;
  expect(ce_test::equal(decoded.datacontenttype(), expected.datacontenttype())) << where;
  expect(ce_test::equal(rendered_extensions(decoded), rendered_extensions(expected))) << where;
  expect(same_payload<C>(decoded, expected)) << where;
}

template<class C>
void check_every_message_decodes(std::string_view label) {
  using namespace boost::ut;
  for (const auto& [name, expected] : mqtt_interop::corpus(go_producer)) {
    const std::string where = std::string{label} + ": go/" + name + ".structured";
    const auto wire = read_message(go_producer, name);
    expect(wire.has_value()) << where << " is missing";
    if (!wire) {
      continue;
    }

    expect(wire->content_type == std::optional<std::string>{std::string{structured_media_type}})
        << where;
    expect(wire->user_properties == 0U) << where << ": structured mode writes no User Property";
    expect(ce::mqtt::detect_content_mode(wire->on_the_wire) == ce::content_mode::structured)
        << where;

    auto decoded = ce::mqtt::from_message<C>(wire->on_the_wire);
    expect(bool{decoded}) << where << " did not decode";
    if (decoded) {
      check_same_event<C>(where, *decoded, expected);
    }

    // The payload alone is the MQTT 3.1.1 PUBLISH: the same event.
    auto from_payload = ce::mqtt::from_payload<C>(wire->on_the_wire);
    expect(bool{from_payload}) << where << " did not decode as a 3.1.1 payload";
    if (from_payload) {
      check_same_event<C>(where, *from_payload, expected);
    }
  }
}

/// What this SDK writes now is the message that was committed for the Go SDK to read, so the
/// committed copy cannot test stale bytes, and Go reads a message of the shape the binding writes.
template<class C>
void check_committed_messages_are_current(std::string_view label) {
  using namespace boost::ut;
  for (const auto& [name, subject] : mqtt_interop::corpus(cpp_producer)) {
    const std::string where = std::string{label} + ": cpp/" + name + ".structured";
    const auto wire = read_message(cpp_producer, name);
    expect(wire.has_value()) << where << " is missing";
    if (!wire) {
      continue;
    }
    auto now = ce::mqtt::to_message<C>(subject, ce::content_mode::structured);
    expect(bool{now}) << where;
    if (!now) {
      continue;
    }
    expect(wire->on_the_wire.header_fields == now->header_fields) << where;
    expect(wire->user_properties == 0U) << where;

    auto decoded = ce::mqtt::from_message<C>(wire->on_the_wire);
    expect(bool{decoded}) << where;
    if (decoded) {
      check_same_event<C>(where, *decoded, subject);
    }
  }
}

/// The payload of `expected` against what a binary-mode decode produced: a JSON payload as a value,
/// anything else as the bytes the message carried, which a binary-mode body is whatever the type.
template<class C>
[[nodiscard]] auto same_binary_payload(const ce::event& decoded, const ce::event& expected)
    -> bool {
  if (std::holds_alternative<ce::json_text>(expected.data())) {
    return same_payload<C>(decoded, expected);
  }
  const auto* bytes = std::get_if<ce::binary>(&decoded.data());
  if (const auto* text = std::get_if<std::string>(&expected.data())) {
    return bytes != nullptr && *bytes == ce::to_bytes(*text);
  }
  if (const auto* wanted = std::get_if<ce::binary>(&expected.data())) {
    return bytes != nullptr && *bytes == *wanted;
  }
  return std::holds_alternative<std::monostate>(decoded.data());
}

template<class C>
void check_same_binary_event(std::string_view where,
                             const ce::event& decoded,
                             const ce::event& expected) {
  using namespace boost::ut;
  expect(decoded.id() == expected.id()) << where;
  expect(bool{decoded.source() == expected.source()}) << where;
  expect(decoded.type() == expected.type()) << where;
  expect(ce_test::equal(decoded.subject(), expected.subject())) << where;
  expect(ce_test::equal(decoded.time(), expected.time())) << where;
  expect(ce_test::equal(decoded.dataschema(), expected.dataschema())) << where;
  expect(ce_test::equal(decoded.datacontenttype(), expected.datacontenttype())) << where;
  // By their text: binary mode carries every extension as a User Property string.
  expect(ce_test::equal(rendered_extensions(decoded), rendered_extensions(expected))) << where;
  expect(same_binary_payload<C>(decoded, expected)) << where;
}

/// The same names with the same values, in any order: the file lists the Content Type property
/// first, and the binding writes it after the attributes. No name repeats in binary mode.
[[nodiscard]] auto same_headers(const ce::raw_headers& left, const ce::raw_headers& right) -> bool {
  if (left.size() != right.size()) {
    return false;
  }
  return std::ranges::all_of(left, [&right](const auto& field) {
    const std::string* other = right.find_exact(field.first);
    return other != nullptr && *other == field.second;
  });
}

/// Every User Property of a binary-mode message Go wrote starts with `ce-`.
void check_go_names(std::string_view where, const recorded& wire) {
  using namespace boost::ut;
  for (const auto& [name, value] : wire.on_the_wire.header_fields) {
    expect(name == content_type_name || std::string_view{name}.starts_with(go_prefix))
        << where << ": " << name;
  }
  expect(wire.on_the_wire.header_fields.contains_exact("ce-specversion")) << where;
}

template<class C>
void check_every_binary_message_decodes(std::string_view label) {
  using namespace boost::ut;
  for (const auto& [name, expected] : mqtt_interop::corpus(go_producer)) {
    const std::string where = std::string{label} + ": go/" + name + ".binary";
    const auto wire = read_message(go_producer, name, binary_mode);
    expect(wire.has_value()) << where << " is missing";
    if (!wire) {
      continue;
    }
    check_go_names(where, *wire);
    expect(wire->content_type ==
           (expected.datacontenttype()
                ? std::optional<std::string>{std::string{expected.datacontenttype()->view()}}
                : std::nullopt))
        << where << ": datacontenttype is the Content Type property";
    expect(ce::mqtt::detect_content_mode(wire->on_the_wire) == ce::content_mode::binary_mode)
        << where;

    auto decoded = ce::mqtt::from_message<C, ce::mqtt::prefixed_names>(wire->on_the_wire);
    expect(bool{decoded}) << where << " did not decode";
    if (decoded) {
      check_same_binary_event<C>(where, *decoded, expected);
    }
  }
}

/// The text's names are the default, and they are not the Go form: each recorded Go binary message
/// is not a CloudEvent to the default reader, and the message of the default writer is not one to
/// the prefixed reader (SWR-MQTT-0015).
template<class C>
void check_default_differs(std::string_view label) {
  using namespace boost::ut;
  for (const auto& [name, expected] : mqtt_interop::corpus(go_producer)) {
    const std::string where = std::string{label} + ": go/" + name + ".binary";
    const auto wire = read_message(go_producer, name, binary_mode);
    if (!wire) {
      expect(false) << where << " is missing";
      continue;
    }
    const auto by_default = ce::mqtt::from_message<C>(wire->on_the_wire);
    expect(!by_default.has_value()) << where << " must not decode under the default names";
    if (!by_default) {
      expect(by_default.error().code == ce::errc::not_a_cloudevent) << where;
    }

    auto ours = ce::mqtt::to_message<C>(expected, ce::content_mode::binary_mode);
    expect(bool{ours}) << where;
    if (ours) {
      const auto by_prefix = ce::mqtt::from_message<C, ce::mqtt::prefixed_names>(*ours);
      expect(!by_prefix.has_value() && by_prefix.error().code == ce::errc::not_a_cloudevent)
          << where << ": the text's names are not the Go form";
    }
  }
}

/// What this SDK writes under `prefixed_names` is the binary message that was committed for the Go
/// SDK to read.
template<class C>
void check_committed_binary_messages_are_current(std::string_view label) {
  using namespace boost::ut;
  for (const auto& [name, subject] : mqtt_interop::corpus(cpp_producer)) {
    const std::string where = std::string{label} + ": cpp/" + name + ".binary";
    const auto wire = read_message(cpp_producer, name, binary_mode);
    expect(wire.has_value()) << where << " is missing";
    if (!wire) {
      continue;
    }
    auto now =
        ce::mqtt::to_message<C, ce::mqtt::prefixed_names>(subject, ce::content_mode::binary_mode);
    expect(bool{now}) << where;
    if (!now) {
      continue;
    }
    expect(same_headers(wire->on_the_wire.header_fields, now->header_fields)) << where;
    expect(ce_test::equal(wire->on_the_wire.body, now->body)) << where;
    check_go_names(where, *wire);

    auto decoded = ce::mqtt::from_message<C, ce::mqtt::prefixed_names>(wire->on_the_wire);
    expect(bool{decoded}) << where;
    if (decoded) {
      check_same_binary_event<C>(where, *decoded, subject);
    }
  }
}

}  // namespace

// spec: SWR-MQTT-0014
const boost::ut::suite<"interop-mqtt-structured-decode"> interop_mqtt_structured_decode = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec} + " decodes every Go message to the event it encodes") = [codec] {
      check_every_message_decodes<C>(codec);
    };
  });
};

// spec: SWR-MQTT-0014
const boost::ut::suite<"interop-mqtt-structured-produce"> interop_mqtt_structured_produce = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec} + " writes the messages that were given to the Go SDK") = [codec] {
      check_committed_messages_are_current<C>(codec);
    };
  });
};

// spec: SWR-MQTT-0015
const boost::ut::suite<"interop-mqtt-binary-prefixed"> interop_mqtt_binary_prefixed = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec} + " decodes every Go binary message under prefixed_names") = [codec] {
      check_every_binary_message_decodes<C>(codec);
    };
    test(std::string{codec} + " writes the binary messages that were given to the Go SDK") =
        [codec] { check_committed_binary_messages_are_current<C>(codec); };
  });
};

// spec: SWR-MQTT-0015
const boost::ut::suite<"interop-mqtt-binary-default-differs"> interop_mqtt_binary_default = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec} + " refuses every Go binary message under the default names") =
        [codec] { check_default_differs<C>(codec); };
  });
};

int main() {}
