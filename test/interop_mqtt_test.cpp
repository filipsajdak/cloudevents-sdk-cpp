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
// event. Binary-mode goldens are in the same directory and are read once the binding reads binary
// mode.

namespace {

using namespace std::string_view_literals;

constexpr auto go_producer = "go"sv;
constexpr auto cpp_producer = "cpp"sv;
constexpr auto structured_media_type = "application/cloudevents+json"sv;
constexpr auto content_type_name = "content-type"sv;

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

[[nodiscard]] auto read_message(std::string_view producer, std::string_view name)
    -> std::optional<recorded> {
  const std::string raw = read_file(ce_fixtures::path(
      "interop/mqtt/" + std::string{producer} + "/" + std::string{name} + ".structured.json"));
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

int main() {}
