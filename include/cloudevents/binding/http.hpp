#pragma once

/// \file
/// \brief The CloudEvents HTTP protocol binding, over a transport-neutral message.

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

/// \brief What the shared binding core needs to know about HTTP.
///
/// A named namespace, not an anonymous one: an anonymous namespace in a header
/// gives every translation unit its own type, which is an ODR violation the
/// linker does not report.
struct http_traits {
  static constexpr std::string_view attribute_prefix = ce::v1::http::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v1::http::detail::content_type_header;
  /// RFC 9110 section 5.1: field names are case-insensitive.
  static constexpr bool case_sensitive_names = false;

  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    return percent_encode(text);
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> result<std::string> {
    return percent_decode(text);
  }
};

static_assert(binding::binding_traits<http_traits>);

}  // namespace detail

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
template <json::json_codec Codec>
[[nodiscard]] auto to_message(const event& subject, content_mode mode) -> result<message> {
  if (auto valid = subject.validate(); !valid) {
    return fail(valid.error().code, valid.error().detail, valid.error().where);
  }

  if (mode == content_mode::structured) {
    return binding::encode_structured<detail::http_traits, Codec>(subject);
  }

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "a batch carries several events; use to_batch_message");
  }

  message out;
  if (auto written = binding::write_attributes<detail::http_traits>(subject, out.header_fields);
      !written) {
    return fail(written.error().code, written.error().detail, written.error().where);
  }
  binding::write_body(subject, out);
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
template <json::json_codec Codec>
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

  auto subject = binding::read_attributes<detail::http_traits>(request.header_fields);
  if (!subject) {
    return fail(subject.error().code, subject.error().detail, subject.error().where);
  }

  if (const std::string* declared = request.header_fields.find(detail::content_type_header);
      declared != nullptr) {
    subject->datacontenttype = *declared;
  }

  binding::read_body(request.body, *subject);

  // Same invariant as the JSON format: an empty ce-type header is present but
  // not valid, and an event that cannot go back out to a message is of no use
  // to a receiver.
  if (auto valid = subject->validate(); !valid) {
    return fail(valid.error().code, valid.error().detail, valid.error().where);
  }

  return std::move(*subject);
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
