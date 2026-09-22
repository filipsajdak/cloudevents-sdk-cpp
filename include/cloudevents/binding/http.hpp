#pragma once

/// \file
/// \brief The CloudEvents HTTP protocol binding, over a transport-neutral message.

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/binding/detail/percent.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1::http {

namespace detail {

inline constexpr std::string_view attribute_prefix = "ce-";
inline constexpr std::string_view content_type_header = "Content-Type";

// The generic helpers moved to where they belong: the text/byte conversions and
// the case-insensitive prefix test are core, and percent-encoding belongs to the
// bindings that escape their field values.
//
// Using-declarations, not wrappers. A using-declaration preserves constexpr, and
// the suites contain static_assert(ce::http::detail::needs_escape(...)); a
// forwarding function would have to repeat every signature to keep that working.
using ce::v1::to_bytes;
using ce::v1::to_text;
using ce::v1::detail::is_valid_utf8;
using ce::v1::detail::starts_with_ignoring_case;
using ce::v1::binding::detail::hex_digit;
using ce::v1::binding::detail::hex_value;
using ce::v1::binding::detail::needs_escape;
using ce::v1::binding::detail::percent_decode;
using ce::v1::binding::detail::percent_encode;

/// Control characters, which a literal header value may not carry: a CR or LF
/// would let a value end the header and begin another. Space is printable and
/// permitted here, so the bound is U+0020 rather than percent.hpp's U+0021.
inline constexpr unsigned char first_printable_ascii = 0x20U;
inline constexpr unsigned char delete_character = 0x7FU;

/// \brief What the shared binding core needs to know about HTTP.
///
/// A named namespace, not an anonymous one: an anonymous namespace in a header
/// gives every translation unit its own type, which is an ODR violation the
/// linker does not report.
template <class Values>
struct http_traits {
  static constexpr std::string_view attribute_prefix = ce::v1::http::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v1::http::detail::content_type_header;
  /// RFC 9110 section 5.1: field names are case-insensitive.
  static constexpr bool case_sensitive_names = false;

  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    return Values::encode(text);
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> result<std::string> {
    return Values::decode(text);
  }
};

}  // namespace detail

/// \brief How a binding renders an attribute value into a header field.
template <class T>
concept value_policy = requires(std::string_view text) {
  { T::encode(text) } -> std::same_as<result<std::string>>;
  { T::decode(text) } -> std::same_as<result<std::string>>;
};

/// \brief The specification's rule, and the default.
///
/// Binding spec section 3.1.3.2: space, double-quote, percent and anything
/// outside U+0021-U+007E are percent-encoded.
struct percent_encoded_values {
  /// Checked on the way out as well as in (SWR-HTTP-0017). Percent-encoding an
  /// ill-formed sequence produces a field a conformant receiver refuses, so
  /// without this the fault is reported to the peer that did not commit it, at a
  /// point where it can neither fix nor attribute it. `decode` has always
  /// checked; producing what this SDK would itself refuse to read is the
  /// asymmetry.
  [[nodiscard]] static auto encode(std::string_view text) -> result<std::string> {
    if (!detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "an attribute value must be well-formed UTF-8");
    }
    return detail::percent_encode(text);
  }

  [[nodiscard]] static auto decode(std::string_view text) -> result<std::string> {
    return detail::percent_decode(text);
  }
};

/// \brief Header values exactly as the Go and Java SDKs write and read them.
///
/// Measured on 2026-09-21 against sdk-go v2.15.2 and sdk-java main: neither
/// encodes on send nor decodes on receive. So a conformant sender is misread by
/// both, and this policy exists for callers who must talk to them. Naming it is
/// a deliberate departure from the binding specification.
///
/// It is not simply "do nothing". Percent-encoding was also what kept a control
/// character out of a header field, and a value carrying CR or LF would let an
/// attacker end the header and start another. This policy refuses one instead.
struct literal_values {
  [[nodiscard]] static auto encode(std::string_view text) -> result<std::string> {
    for (const char character : text) {
      if (static_cast<unsigned char>(character) < detail::first_printable_ascii ||
          static_cast<unsigned char>(character) == detail::delete_character) {
        return fail(errc::invalid_argument,
                    "a literal header value may not contain a control character");
      }
    }
    if (!detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "a header value must be well-formed UTF-8");
    }
    return std::string{text};
  }

  [[nodiscard]] static auto decode(std::string_view text) -> result<std::string> {
    if (!detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "a header value must be well-formed UTF-8");
    }
    return std::string{text};
  }
};

static_assert(binding::binding_traits<detail::http_traits<percent_encoded_values>>);
static_assert(binding::binding_traits<detail::http_traits<literal_values>>);

/// \brief Which content mode a received message uses.
///
/// Decided by Content-Type prefix. The batch prefix is tested first, because
/// `application/cloudevents-batch+json` also starts with `application/cloudevents`.
[[nodiscard]] inline auto detect_content_mode(const message& request) -> content_mode {
  const std::string* declared = request.header_fields.find(detail::content_type_header);
  if (declared == nullptr) {
    return content_mode::binary_mode;
  }
  if (detail::starts_with_ignoring_case(*declared, "application/cloudevents-batch")) {
    return content_mode::batched;
  }
  if (detail::starts_with_ignoring_case(*declared, "application/cloudevents")) {
    return content_mode::structured;
  }
  return content_mode::binary_mode;
}

/// \brief Lay an event out as an HTTP message.
template <json::json_codec Codec, value_policy Values = percent_encoded_values>
[[nodiscard]] auto to_message(const event& cloud_event, content_mode mode) -> result<message> {
  if (mode == content_mode::structured) {
    return binding::encode_structured<detail::http_traits<Values>, Codec>(cloud_event);
  }

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "a batch carries several events; use to_batch_message");
  }

  message out;
  if (auto written =
          binding::write_attributes<detail::http_traits<Values>>(cloud_event, out.header_fields);
      !written) {
    return fail(written.error().code, written.error().detail, written.error().where);
  }
  binding::write_body(cloud_event, out);
  return out;
}

/// \brief Lay a batch out as a structured batch message.
///
/// Batch is HTTP's alone among the bindings this SDK implements, so it stays here
/// rather than in the shared core.
template <json::json_codec Codec>
[[nodiscard]] auto to_batch_message(std::span<const event> events) -> result<message> {
  auto text = json_format<Codec>::encode_batch(events);
  if (!text) {
    return fail(text.error().code, text.error().detail, text.error().where);
  }
  message out;
  out.header_fields.set(std::string{detail::content_type_header},
                        std::string{json::batch_content_type});
  out.body = detail::to_bytes(*text);
  return out;
}

/// \brief Read an event from an HTTP message.
///
/// A request with no `ce-specversion` and a content type that is not a
/// CloudEvents one is `not_a_cloudevent`, which a receiver usually wants to pass
/// through rather than reject. That is a different outcome from malformed.
template <json::json_codec Codec, value_policy Values = percent_encoded_values>
[[nodiscard]] auto from_message(const message& request) -> result<event> {
  const content_mode mode = detect_content_mode(request);

  if (mode == content_mode::structured) {
    return binding::decode_structured<Codec>(request);
  }
  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "this message is a batch; use from_batch_message");
  }

  if (!request.header_fields.contains("ce-specversion")) {
    return fail(errc::not_a_cloudevent, "no ce-specversion header and no CloudEvents content type");
  }

  auto under_construction =
      binding::read_attributes<detail::http_traits<Values>>(request.header_fields);
  if (!under_construction) {
    return fail(under_construction.error().code, under_construction.error().detail,
                under_construction.error().where);
  }

  if (const std::string* declared = request.header_fields.find(detail::content_type_header);
      declared != nullptr) {
    auto media_type = datacontenttype::make(*declared);
    if (!media_type) {
      return fail(media_type.error().code, media_type.error().detail, media_type.error().where);
    }
    under_construction->rest.datacontenttype = std::move(*media_type);
  }

  under_construction->rest.data =
      binding::read_body(request.body, under_construction->rest.datacontenttype);

  return std::move(*under_construction).build();
}

/// \brief Read a batch from an HTTP message.
template <json::json_codec Codec>
[[nodiscard]] auto from_batch_message(const message& request) -> result<std::vector<event>> {
  if (detect_content_mode(request) != content_mode::batched) {
    return fail(errc::not_a_cloudevent, "the content type is not a CloudEvents batch");
  }
  return json_format<Codec>::decode_batch(detail::to_text(request.body));
}

}  // namespace ce::inline v1::http
