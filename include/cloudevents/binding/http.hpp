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

  message out;

  if (mode == content_mode::structured) {
    auto text = json_format<Codec>::encode(subject);
    if (!text) {
      return fail(text.error().code, text.error().detail, text.error().where);
    }
    out.header_fields.set(std::string{detail::content_type_header},
                          std::string{json::content_type});
    out.body = detail::to_bytes(*text);
    return out;
  }

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "a batch carries several events; use to_batch_message");
  }

  // Binary mode: attributes become ce- headers, and the payload is the body.
  const auto put = [&out](std::string_view name, std::string_view value) {
    out.header_fields.set(std::string{detail::attribute_prefix} + std::string{name},
                          detail::percent_encode(value));
  };

  put("specversion", subject.specversion);
  put("id", subject.id);
  put("source", subject.source.view());
  put("type", subject.type);
  if (subject.dataschema) {
    put("dataschema", subject.dataschema->view());
  }
  if (subject.subject) {
    put("subject", *subject.subject);
  }
  if (subject.time) {
    put("time", to_string(*subject.time));
  }

  for (const auto& [name, attribute] : subject.extensions) {
    auto rendered = std::visit(
        [](const auto& held) -> std::string {
          using held_type = std::remove_cvref_t<decltype(held)>;
          if constexpr (std::is_same_v<held_type, bool>) {
            return held ? "true" : "false";
          } else if constexpr (std::is_same_v<held_type, std::int32_t>) {
            return std::to_string(held);
          } else if constexpr (std::is_same_v<held_type, std::string>) {
            return held;
          } else if constexpr (std::is_same_v<held_type, binary>) {
            return base64_encode(held);
          } else if constexpr (std::is_same_v<held_type, timestamp>) {
            return to_string(held);
          } else {
            return std::string{held.view()};
          }
        },
        attribute);
    put(name, rendered);
  }

  // datacontenttype travels as Content-Type and must not also appear as
  // ce-datacontenttype, or a receiver sees the same attribute twice.
  if (subject.datacontenttype) {
    out.header_fields.set(std::string{detail::content_type_header}, *subject.datacontenttype);
  }

  std::visit(
      [&out](const auto& held) {
        using held_type = std::remove_cvref_t<decltype(held)>;
        if constexpr (std::is_same_v<held_type, std::monostate>) {
          // no body
        } else if constexpr (std::is_same_v<held_type, binary>) {
          out.body = held;
        } else if constexpr (std::is_same_v<held_type, std::string>) {
          out.body = detail::to_bytes(held);
        } else {
          out.body = detail::to_bytes(held.raw);
        }
      },
      subject.data);

  return out;
}

/// \brief Lay a batch out as a structured batch message.
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
    return json_format<Codec>::decode(detail::to_text(request.body));
  }
  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "this message is a batch; use from_batch_message");
  }

  if (!request.header_fields.contains("ce-specversion")) {
    return fail(errc::not_a_cloudevent, "no ce-specversion header and no CloudEvents content type");
  }

  event subject{.id = {}, .source = {}, .type = {}};
  bool saw_id = false;
  bool saw_source = false;
  bool saw_type = false;
  result<void> header_error{};

  for (const auto& [name, raw_value] : request.header_fields) {
    if (!detail::starts_with_ignoring_case(name, detail::attribute_prefix)) {
      continue;
    }
    const std::string attribute = [&name] {
      std::string lowered{name.substr(detail::attribute_prefix.size())};
      for (char& character : lowered) {
        character = ce::detail::ascii_lower(character);
      }
      return lowered;
    }();

    auto decoded = detail::percent_decode(raw_value);
    if (!decoded) {
      header_error = fail(decoded.error().code, decoded.error().detail, attribute);
      break;
    }

    if (attribute == "specversion") {
      subject.specversion = *decoded;
    } else if (attribute == "id") {
      subject.id = *decoded;
      saw_id = true;
    } else if (attribute == "source") {
      subject.source = uri_ref{*decoded};
      saw_source = true;
    } else if (attribute == "type") {
      subject.type = *decoded;
      saw_type = true;
    } else if (attribute == "dataschema") {
      subject.dataschema = uri{*decoded};
    } else if (attribute == "subject") {
      subject.subject = *decoded;
    } else if (attribute == "time") {
      auto parsed = parse_timestamp(*decoded);
      if (!parsed) {
        header_error = fail(parsed.error().code, parsed.error().detail, "time");
        break;
      }
      subject.time = *parsed;
    } else {
      // Same rule as the JSON format: a ce- header whose name is not one the
      // spec allows cannot become an extension, or from_message would return an
      // event that to_message then refuses.
      if (!valid_attribute_name(attribute)) {
        header_error = fail(errc::invalid_attribute_name,
                            "extension names must match [a-z0-9]+", attribute);
        break;
      }
      // The wire form carries no type, so an extension arrives as a string. The
      // typed extension structs are what recover the declared type.
      subject.extensions.insert_or_assign(attribute, attribute_value{*decoded});
    }
  }

  if (!header_error) {
    return fail(header_error.error().code, header_error.error().detail, header_error.error().where);
  }
  if (subject.specversion != "1.0") {
    return fail(errc::unsupported_spec_version, "this SDK implements CloudEvents 1.0 only",
                "specversion");
  }
  if (!saw_id || !saw_source || !saw_type) {
    return fail(errc::missing_required_attribute, "id, source and type are all required",
                !saw_id ? "id" : (!saw_source ? "source" : "type"));
  }

  if (const std::string* declared = request.header_fields.find(detail::content_type_header);
      declared != nullptr) {
    subject.datacontenttype = *declared;
  }

  if (!request.body.empty()) {
    const bool json_payload =
        subject.datacontenttype && is_json_content_type(*subject.datacontenttype);
    if (json_payload) {
      subject.data = json_text{.raw = detail::to_text(request.body)};
    } else {
      subject.data = request.body;
    }
  }

  // Same invariant as the JSON format: an empty ce-type header is present but
  // not valid, and an event that cannot go back out to a message is of no use
  // to a receiver.
  if (auto valid = subject.validate(); !valid) {
    return fail(valid.error().code, valid.error().detail, valid.error().where);
  }

  return subject;
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
