#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/decode_options.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

// spec: SYS-MQTT-0001
/// The CloudEvents binding for MQTT, in structured mode. A PUBLISH is a `message`: the payload is
/// the body, the Content Type property is the header `content-type` and the User Properties are the
/// other headers, in order. No MQTT client type is named.
///
/// MQTT 5.0 is `to_message` and `from_message`. MQTT 3.1.1 has no properties, so a message with no
/// headers cannot be told from a 5.0 message with none: the caller names the protocol version by
/// the entry point, and 3.1.1 is `to_payload` and `from_payload`.
namespace ce::inline v4::mqtt {

namespace detail {

// spec: SWR-MQTT-0003
inline constexpr std::string_view content_type_header = "content-type";
inline constexpr std::string_view structured_media_type_prefix = "application/cloudevents";
inline constexpr std::string_view batch_media_type_prefix = "application/cloudevents-batch";

struct mqtt_traits {
  static constexpr std::string_view attribute_prefix = {};
  static constexpr std::string_view content_type_header = ce::v4::mqtt::detail::content_type_header;
  // spec: SWR-MQTT-0007
  static constexpr bool case_sensitive_names = true;

  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    if (!ce::v4::detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "an MQTT User Property value must be a UTF-8 string");
    }
    return std::string{text};
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> result<std::string> {
    return encode_value(text);
  }
};

static_assert(binding::binding_traits<mqtt_traits>);

}  // namespace detail

// spec: SWR-MQTT-0004
// spec: SWR-MQTT-0007
/// The mode a 5.0 message declares by its Content Type property: `structured` for a media type
/// starting with `application/cloudevents`, `batched` for one starting with
/// `application/cloudevents-batch`, and `binary_mode` for any other or none. Names compare byte for
/// byte, so `Content-Type` is another name.
[[nodiscard]] inline auto detect_content_mode(const message& incoming) -> content_mode {
  const std::string* declared = incoming.header_fields.find_exact(detail::content_type_header);
  if (declared == nullptr) {
    return content_mode::binary_mode;
  }
  if (std::string_view{*declared}.starts_with(detail::batch_media_type_prefix)) {
    return content_mode::batched;
  }
  if (std::string_view{*declared}.starts_with(detail::structured_media_type_prefix)) {
    return content_mode::structured;
  }
  return content_mode::binary_mode;
}

// spec: SWR-MQTT-0001
// spec: SWR-MQTT-0006
/// An event as an MQTT 5.0 message in structured mode: the JSON event format as the body, the
/// Content Type `application/cloudevents+json` as the only header, and no User Property. Binary
/// mode and batch mode are refused with `errc::invalid_argument`.
template<json::json_codec Codec>
[[nodiscard]] auto to_message(const event& cloud_event, content_mode mode) -> result<message> {
  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "the MQTT binding defines no batch mode");
  }
  if (mode != content_mode::structured) {
    return fail(errc::invalid_argument, "the MQTT binding writes structured mode only");
  }
  return binding::encode_structured<detail::mqtt_traits, Codec>(cloud_event);
}

// spec: SWR-MQTT-0001
// spec: SWR-MQTT-0004
// spec: SWR-MQTT-0005
// spec: SWR-MQTT-0006
// spec: SWR-MQTT-0008
/// An event from an MQTT 5.0 message in structured mode. Every User Property is ignored. A message
/// with a header named `content-type` more than once is refused with `errc::invalid_argument`,
/// because a User Property of that name cannot be told from the Content Type property. A batch
/// Content Type is `errc::invalid_argument`; a message that declares no CloudEvents Content Type is
/// `errc::not_a_cloudevent`; a payload that does not parse is the codec's error.
template<json::json_codec Codec>
[[nodiscard]] auto from_message(const message& incoming, json::decode_options options = {})
    -> result<event> {
  const auto named_content_type = [](const raw_headers::entry& field) {
    return field.first == detail::content_type_header;
  };
  if (std::ranges::count_if(incoming.header_fields, named_content_type) > 1) {
    return fail(errc::invalid_argument,
                "a User Property named content-type beside the Content Type property");
  }

  switch (detect_content_mode(incoming)) {
    case content_mode::batched:
      return fail(errc::invalid_argument, "the MQTT binding defines no batch mode");
    case content_mode::structured:
      return binding::decode_structured<Codec>(incoming, options);
    case content_mode::binary_mode:
      break;
  }
  return fail(errc::not_a_cloudevent, "no CloudEvents Content Type property");
}

// spec: SWR-MQTT-0002
/// An event as the payload of an MQTT 3.1.1 PUBLISH: the bytes of the JSON event format. MQTT 3.1.1
/// has no Content Type and no User Properties.
template<json::json_codec Codec>
[[nodiscard]] auto to_payload(const event& cloud_event) -> result<std::string> {
  return json_format<Codec>::encode(cloud_event);
}

// spec: SWR-MQTT-0002
/// An event from the payload of an MQTT 3.1.1 PUBLISH, read as the JSON event format.
template<json::json_codec Codec>
[[nodiscard]] auto from_payload(std::string_view payload, json::decode_options options = {})
    -> result<event> {
  return json_format<Codec>::decode(payload, options);
}

// spec: SWR-MQTT-0002
/// `from_payload` for the body of a message, whatever headers the message carries.
template<json::json_codec Codec>
[[nodiscard]] auto from_payload(const message& incoming, json::decode_options options = {})
    -> result<event> {
  return binding::decode_structured<Codec>(incoming, options);
}

}  // namespace ce::inline v4::mqtt
