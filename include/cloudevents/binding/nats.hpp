#pragma once

/// \file
/// \brief The CloudEvents NATS protocol binding.
///
/// The smallest binding in the SDK, and the two facts a caller will get wrong are
/// both here rather than in a specification they have not read.
///
/// **Binary mode needs NATS 2.2.** The binding specification used to say NATS
/// "will only support _structured_ data mode at this time", because the protocol
/// had no message headers. It gained them in 2.2, and the specification now says
/// "Every compliant implementation SHOULD support both structured and binary
/// modes".
///
/// So this header has two pairs. `to_payload` and `from_payload` are structured
/// mode and predate binary mode; they exchange the JSON event as text, which is
/// all a pre-2.2 server can carry. `to_message` and `from_message` are the
/// general form, and a caller on 2.2 or later wants those.
///
/// **The cloud_event is yours.** The specification defines no mapping from an event
/// to a NATS cloud_event, so this binding does not invent one. Publishing to a
/// cloud_event is the application's, exactly as issuing an HTTP request is.

#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/binding/detail/percent.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1::nats {

/// \brief The NATS message payload for an event: UTF-8 JSON text.
///
/// `ce::to_bytes` converts it where a client wants bytes.
template <json::json_codec Codec>
[[nodiscard]] auto to_payload(const event& cloud_event) -> result<std::string> {
  return json_format<Codec>::encode(cloud_event);
}

/// \brief Read an event from a NATS message payload.
///
/// **A payload that is not a CloudEvent cannot be told from a malformed one.**
/// The HTTP and Kafka bindings answer `errc::not_a_cloudevent` because they have
/// a content type or a `ce_specversion` header to consult; a NATS payload carries
/// neither, so an unrelated JSON document is indistinguishable from a corrupt
/// event and every failure here is a parse or validation error. A caller who
/// needs the distinction has to carry it in the cloud_event.
template <json::json_codec Codec>
[[nodiscard]] auto from_payload(std::string_view payload) -> result<event> {
  return json_format<Codec>::decode(payload);
}

namespace detail {

inline constexpr std::string_view attribute_prefix = "ce-";
inline constexpr std::string_view content_type_header = "Content-Type";

/// \brief What the shared binding core needs to know about NATS.
///
/// Two rules set it apart from HTTP, and both come from the binding text.
///
/// datacontenttype is **not** special here. Every attribute maps "with the same
/// name as the attribute name but prefixed with `ce-`", datacontenttype
/// included, so it travels as `ce-datacontenttype` and is percent-encoded like
/// any other value. `Content-Type` is left to say that a message is structured.
///
/// Header values are percent-encoded, by the same rule as HTTP: space, double
/// quote, percent, and anything outside U+0021-U+007E.
struct nats_traits {
  static constexpr std::string_view attribute_prefix = ce::v1::nats::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v1::nats::detail::content_type_header;
  /// The binding matches `Content-Type` case-insensitively and NATS headers
  /// carry HTTP's field-name conventions, so names are not byte-exact here.
  static constexpr bool case_sensitive_names = false;
  static constexpr bool content_type_is_attribute = true;

  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    return binding::detail::percent_encode(text);
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> result<std::string> {
    return binding::detail::percent_decode(text);
  }
};

static_assert(binding::binding_traits<nats_traits>);

}  // namespace detail

/// \brief Which content mode a received message uses.
///
/// The binding inverts HTTP's default: a CloudEvents content type means
/// structured, and **anything else means binary**, including no content type at
/// all. There is no batch mode.
[[nodiscard]] inline auto detect_content_mode(const message& incoming) -> content_mode {
  const std::string* declared = incoming.header_fields.find(detail::content_type_header);
  if (declared != nullptr &&
      ce::v1::detail::starts_with_ignoring_case(*declared, "application/cloudevents")) {
    return content_mode::structured;
  }
  return content_mode::binary_mode;
}

/// \brief Lay an event out as a NATS message.
///
/// Binary mode needs a server at NATS 2.2 or later, because it carries the
/// attributes in headers. `content_mode::batched` is refused: the binding
/// defines no batch mode.
template <json::json_codec Codec>
[[nodiscard]] auto to_message(const event& cloud_event, content_mode mode) -> result<message> {
  if (auto valid = cloud_event.validate(); !valid) {
    return fail(valid.error().code, valid.error().detail, valid.error().where);
  }

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "the NATS binding defines no batch mode");
  }

  if (mode == content_mode::structured) {
    return binding::encode_structured<detail::nats_traits, Codec>(cloud_event);
  }

  message out;
  if (auto written = binding::write_attributes<detail::nats_traits>(cloud_event, out.header_fields);
      !written) {
    return fail(written.error().code, written.error().detail, written.error().where);
  }
  binding::write_body(cloud_event, out);
  return out;
}

/// \brief Read an event from a NATS message.
///
/// Unlike `from_payload`, this one can answer `not_a_cloudevent`: a binary-mode
/// message carries `ce-specversion`, so its absence is a message that was never
/// a CloudEvent rather than one that is malformed.
template <json::json_codec Codec>
[[nodiscard]] auto from_message(const message& incoming) -> result<event> {
  if (detect_content_mode(incoming) == content_mode::structured) {
    return binding::decode_structured<Codec>(incoming);
  }

  if (!incoming.header_fields.contains("ce-specversion")) {
    return fail(errc::not_a_cloudevent,
                "no ce-specversion header and no CloudEvents content type");
  }

  auto cloud_event = binding::read_attributes<detail::nats_traits>(incoming.header_fields);
  if (!cloud_event) {
    return fail(cloud_event.error().code, cloud_event.error().detail, cloud_event.error().where);
  }

  binding::read_body(incoming.body, *cloud_event);

  if (auto valid = cloud_event->validate(); !valid) {
    return fail(valid.error().code, valid.error().detail, valid.error().where);
  }
  return std::move(*cloud_event);
}

}  // namespace ce::inline v1::nats
