#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
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
/// The CloudEvents binding for MQTT. A PUBLISH is a `message`: the payload is the body, the
/// Content Type property is the header `content-type` and the User Properties are the other
/// headers, in order. No MQTT client type is named.
///
/// MQTT 5.0 is `to_message` and `from_message`, in structured and in binary mode. MQTT 3.1.1 has no
/// properties, so a message with no headers cannot be told from a 5.0 message with none: the
/// caller names the protocol version by the entry point, and 3.1.1 is `to_payload` and
/// `from_payload`, always structured.
namespace ce::inline v4::mqtt {

namespace detail {

// spec: SWR-MQTT-0003
inline constexpr std::string_view content_type_header = "content-type";
inline constexpr std::string_view structured_media_type_prefix = "application/cloudevents";
inline constexpr std::string_view batch_media_type_prefix = "application/cloudevents-batch";
inline constexpr std::string_view version_attribute = "specversion";
inline constexpr std::string_view media_type_attribute = "datacontenttype";

// spec: SWR-MQTT-0011
/// The most bytes an MQTT UTF-8 Encoded String can hold: its length is a Two Byte Integer
/// (OASIS MQTT 5.0, 1.5.4).
inline constexpr std::size_t max_string_bytes = std::numeric_limits<std::uint16_t>::max();

template<class Names>
struct mqtt_traits {
  static constexpr std::string_view attribute_prefix = Names::attribute_prefix;
  static constexpr std::string_view content_type_header = ce::v4::mqtt::detail::content_type_header;
  // spec: SWR-MQTT-0007
  static constexpr bool case_sensitive_names = true;

  // spec: SWR-MQTT-0011
  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    if (!ce::v4::detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "an MQTT User Property value must be a UTF-8 string");
    }
    if (text.find('\0') != std::string_view::npos) {
      return fail(errc::invalid_argument, "an MQTT User Property value may not contain NUL");
    }
    if (text.size() > max_string_bytes) {
      return fail(errc::invalid_argument, "an MQTT User Property value is at most 65535 bytes");
    }
    return std::string{text};
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> result<std::string> {
    return encode_value(text);
  }
};

}  // namespace detail

// spec: SWR-MQTT-0012
/// A name policy says how binary mode spells an attribute as a User Property.
template<class T>
concept name_policy = requires {
  { T::attribute_prefix } -> std::convertible_to<std::string_view>;
};

// spec: SWR-MQTT-0009
/// The names of the CloudEvents MQTT binding text (`:168-169`): `specversion`, `id`, `type`. The
/// default of every binary-capable entry point.
struct unprefixed_names {
  static constexpr std::string_view attribute_prefix = {};
};

// spec: SWR-MQTT-0012
/// The names the Go SDK writes and requires (`ce-specversion`), an opt-in for talking to it.
struct prefixed_names {
  static constexpr std::string_view attribute_prefix = "ce-";
};

static_assert(name_policy<unprefixed_names> && name_policy<prefixed_names>);
static_assert(binding::binding_traits<detail::mqtt_traits<unprefixed_names>>);
static_assert(binding::binding_traits<detail::mqtt_traits<prefixed_names>>);

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

namespace detail {

// spec: SWR-MQTT-0013
/// What a User Property is to the binary reader under a name policy.
enum class field_kind : std::uint8_t {
  foreign,          ///< not an attribute under the policy's naming: ignored
  attribute,        ///< an attribute or an extension
  media_type_copy,  ///< `datacontenttype` as a User Property under the unprefixed names
};

template<name_policy Names>
[[nodiscard]] auto kind_of(std::string_view name) -> field_kind {
  constexpr std::string_view policy_prefix = Names::attribute_prefix;
  if (!name.starts_with(policy_prefix) ||
      !valid_attribute_name(name.substr(policy_prefix.size()))) {
    return field_kind::foreign;
  }
  if (policy_prefix.empty() && name == media_type_attribute) {
    return field_kind::media_type_copy;
  }
  return field_kind::attribute;
}

// spec: SWR-MQTT-0010
/// The `datacontenttype` User Property that example 3.1.4 of the text lists beside the Content
/// Type property: accepted when it is the Content Type, refused when it differs, is the property's
/// absence or comes a second time.
[[nodiscard]] inline auto accept_media_type_copy(const message& incoming,
                                                 std::string_view value,
                                                 bool& already_seen) -> result<void> {
  const std::string* declared = incoming.header_fields.find_exact(content_type_header);
  if (already_seen || declared == nullptr || *declared != value) {
    return fail(errc::invalid_argument,
                "a datacontenttype User Property must equal the Content Type property, once",
                std::string{media_type_attribute});
  }
  already_seen = true;
  return {};
}

// spec: SWR-MQTT-0010
// spec: SWR-MQTT-0013
/// The User Properties that are attributes under the policy, in order, for the binding core to
/// read. A foreign property is left out, so it can neither fail the message nor collide.
template<name_policy Names>
[[nodiscard]] auto attribute_fields(const message& incoming) -> result<raw_headers> {
  raw_headers fields;
  bool media_type_seen = false;
  for (const auto& [name, value] : incoming.header_fields) {
    switch (kind_of<Names>(name)) {
      case field_kind::foreign:
        break;
      case field_kind::attribute:
        fields.add(name, value);
        break;
      case field_kind::media_type_copy:
        if (auto accepted = accept_media_type_copy(incoming, value, media_type_seen); !accepted) {
          return ce::v4::detail::forward_failure(std::move(accepted).error());
        }
        break;
    }
  }
  return fields;
}

template<name_policy Names>
[[nodiscard]] auto carries_version(const message& incoming) -> bool {
  return incoming.header_fields.contains_exact(
      std::string{Names::attribute_prefix}.append(version_attribute));
}

// spec: SWR-MQTT-0010
template<json::json_codec Codec, name_policy Names>
[[nodiscard]] auto decode_binary(const message& incoming, const json::decode_options& options)
    -> result<event> {
  auto fields = attribute_fields<Names>(incoming);
  if (!fields) {
    return ce::v4::detail::forward_failure(std::move(fields).error());
  }
  auto under_construction = binding::read_attributes<mqtt_traits<Names>>(*fields);
  if (!under_construction) {
    return ce::v4::detail::forward_failure(std::move(under_construction).error());
  }

  if (const std::string* declared = incoming.header_fields.find_exact(content_type_header);
      declared != nullptr) {
    auto media_type = datacontenttype::make(*declared);
    if (!media_type) {
      return ce::v4::detail::forward_failure(std::move(media_type).error());
    }
    under_construction->rest.datacontenttype = std::move(*media_type);
  }

  auto payload =
      binding::read_body<Codec>(incoming.body, under_construction->rest.datacontenttype, options);
  if (!payload) {
    return ce::v4::detail::forward_failure(std::move(payload).error());
  }
  under_construction->rest.data = std::move(*payload);
  return std::move(*under_construction).build();
}

template<json::json_codec Codec, name_policy Names>
[[nodiscard]] auto encode_binary(const event& cloud_event) -> result<message> {
  message out;
  if (auto written = binding::write_attributes<mqtt_traits<Names>>(cloud_event, out.header_fields);
      !written) {
    return ce::v4::detail::forward_failure(std::move(written).error());
  }
  binding::write_body(cloud_event, out);
  return out;
}

}  // namespace detail

// spec: SWR-MQTT-0001
// spec: SWR-MQTT-0006
// spec: SWR-MQTT-0009
// spec: SWR-MQTT-0012
/// An event as an MQTT 5.0 message. Structured mode is the JSON event format as the body and the
/// Content Type `application/cloudevents+json` as the only header. Binary mode is the data as the
/// body, `datacontenttype` as the Content Type property and every other attribute and extension as
/// a User Property named as `Names` says: unchanged by default, `ce-` followed by the name for
/// `prefixed_names`. Batch mode is refused with `errc::invalid_argument`. A value that is not
/// well-formed UTF-8 is `errc::invalid_utf8`; one with NUL or over 65,535 bytes is
/// `errc::invalid_argument`.
template<json::json_codec Codec, name_policy Names = unprefixed_names>
[[nodiscard]] auto to_message(const event& cloud_event, content_mode mode) -> result<message> {
  switch (mode) {
    case content_mode::batched:
      return fail(errc::invalid_argument, "the MQTT binding defines no batch mode");
    case content_mode::structured:
      return binding::encode_structured<detail::mqtt_traits<Names>, Codec>(cloud_event);
    case content_mode::binary_mode:
      break;
  }
  return detail::encode_binary<Codec, Names>(cloud_event);
}

// spec: SWR-MQTT-0001
// spec: SWR-MQTT-0004
// spec: SWR-MQTT-0005
// spec: SWR-MQTT-0006
// spec: SWR-MQTT-0008
// spec: SWR-MQTT-0010
// spec: SWR-MQTT-0013
/// An event from an MQTT 5.0 message. A message with a header named `content-type` more than once
/// is refused with `errc::invalid_argument`, because a User Property of that name cannot be told
/// from the Content Type property. A batch Content Type is `errc::invalid_argument`. A
/// CloudEvents Content Type makes the message structured and every User Property is ignored. Any
/// other message is binary when it carries the `specversion` User Property under `Names`, and
/// `errc::not_a_cloudevent` when it does not. In binary mode a User Property that is not an
/// attribute name under `Names` is ignored and the others are attributes and extensions, read as
/// strings.
template<json::json_codec Codec, name_policy Names = unprefixed_names>
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
  if (!detail::carries_version<Names>(incoming)) {
    return fail(errc::not_a_cloudevent,
                "no CloudEvents Content Type property and no specversion User Property");
  }
  return detail::decode_binary<Codec, Names>(incoming, options);
}

// spec: SWR-MQTT-0002
/// An event as the payload of an MQTT 3.1.1 PUBLISH: the bytes of the JSON event format. MQTT 3.1.1
/// has no Content Type and no User Properties, so it has no binary mode.
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
