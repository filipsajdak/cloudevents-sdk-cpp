#pragma once

/// \file
/// \brief The parts every protocol binding shares, parameterised by a traits type.
///
/// CloudEvents describes HTTP, Kafka, AMQP, MQTT, NATS and WebSockets the same
/// way: attributes become named fields under a prefix, the payload becomes the
/// body, and structured mode puts the whole event in the body under a content
/// type. What differs between them fits in the traits below.

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1::binding {

/// \brief What the shared core needs to know about one protocol.
///
/// `case_sensitive_names` drives both directions rather than only one. HTTP field
/// names are case-insensitive, while Kafka record headers, AMQP
/// application-properties and MQTT user properties are not, and a binding that
/// lowered a name on the way in but preserved it on the way out would round-trip
/// wrongly. One flag makes that pair impossible to get wrong.
template <class T>
concept binding_traits = requires(std::string_view text) {
  { T::attribute_prefix } -> std::convertible_to<std::string_view>;
  { T::content_type_header } -> std::convertible_to<std::string_view>;
  { T::case_sensitive_names } -> std::convertible_to<bool>;
  { T::encode_value(text) } -> std::same_as<result<std::string>>;
  { T::decode_value(text) } -> std::same_as<result<std::string>>;
};

namespace detail {

/// \brief Whether a binding carries datacontenttype as a prefixed attribute.
///
/// HTTP and Kafka put it in an unprefixed content-type field and must not also
/// emit it under the prefix. NATS does the opposite: the binding maps every
/// attribute including datacontenttype "with the same name as the attribute name
/// but prefixed with `ce-`", and reserves the content-type field for saying that
/// a message is in structured mode.
///
/// Detected rather than required, so a traits type written before this existed
/// still satisfies `binding_traits` and still means what it meant.
template <class T>
concept declares_content_type_as_attribute = requires {
  { T::content_type_is_attribute } -> std::convertible_to<bool>;
};

template <class T>
struct content_type_policy {
  static constexpr bool as_attribute = false;
};

template <declares_content_type_as_attribute T>
struct content_type_policy<T> {
  static constexpr bool as_attribute = static_cast<bool>(T::content_type_is_attribute);
};

template <binding_traits T>
void put(headers& into, std::string name, std::string value) {
  if constexpr (T::case_sensitive_names) {
    into.set_exact(std::move(name), std::move(value));
  } else {
    into.set(std::move(name), std::move(value));
  }
}

template <binding_traits T>
[[nodiscard]] auto look_up(const headers& fields, std::string_view name) -> const std::string* {
  if constexpr (T::case_sensitive_names) {
    return fields.find_exact(name);
  } else {
    return fields.find(name);
  }
}

template <binding_traits T>
[[nodiscard]] auto carries_prefix(std::string_view name) -> bool {
  if constexpr (T::case_sensitive_names) {
    return name.starts_with(std::string_view{T::attribute_prefix});
  } else {
    return ce::v1::detail::starts_with_ignoring_case(name, T::attribute_prefix);
  }
}

template <binding_traits T>
[[nodiscard]] auto attribute_name_of(std::string_view field) -> std::string {
  std::string name{field.substr(std::string_view{T::attribute_prefix}.size())};
  if constexpr (!T::case_sensitive_names) {
    for (char& character : name) {
      character = ce::v1::detail::ascii_lower(character);
    }
  }
  return name;
}

}  // namespace detail

/// \brief An attribute in the text form every binding puts on the wire.
[[nodiscard]] inline auto render_attribute(const attribute_value& value) -> std::string {
  return std::visit(
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
      value);
}

/// \brief Lay an event's attributes out as prefixed fields.
///
/// The emission order is part of the contract: specversion, id, source, type,
/// dataschema, cloud_event, time, the extensions, and the content type last. The
/// interop and conformance fixtures compare whole messages, so a reordering that
/// reads as tidying breaks them.
template <binding_traits T>
[[nodiscard]] auto write_attributes(const event& cloud_event, headers& into) -> result<void> {
  result<void> failure{};

  const auto put_attribute = [&into, &failure](std::string_view name, std::string_view value) {
    if (!failure) {
      return;
    }
    auto encoded = T::encode_value(value);
    if (!encoded) {
      failure = fail(encoded.error().code, encoded.error().detail, std::string{name});
      return;
    }
    detail::put<T>(into, std::string{T::attribute_prefix} + std::string{name}, std::move(*encoded));
  };

  put_attribute("specversion", cloud_event.specversion);
  put_attribute("id", cloud_event.id);
  put_attribute("source", cloud_event.source.view());
  put_attribute("type", cloud_event.type);
  if (cloud_event.dataschema) {
    put_attribute("dataschema", cloud_event.dataschema->view());
  }
  if (cloud_event.subject) {
    put_attribute("subject", *cloud_event.subject);
  }
  if (cloud_event.time) {
    put_attribute("time", to_string(*cloud_event.time));
  }
  for (const auto& [name, attribute] : cloud_event.extensions) {
    put_attribute(name, render_attribute(attribute));
  }

  if (!failure) {
    return failure;
  }

  // Where datacontenttype travels as the content-type field it must not also
  // appear under the prefix, or a receiver sees the same attribute twice, and it
  // is not encoded there: it is a media type, not an attribute value. Where the
  // binding maps it like any other attribute, it is encoded like one.
  if (cloud_event.datacontenttype) {
    if constexpr (detail::content_type_policy<T>::as_attribute) {
      put_attribute("datacontenttype", *cloud_event.datacontenttype);
    } else {
      detail::put<T>(into, std::string{T::content_type_header}, *cloud_event.datacontenttype);
    }
  }
  return failure;
}

/// \brief Read the attributes out of a message's fields.
///
/// The event is returned without its datacontenttype, its payload or a
/// `validate()` call: those need the body, which is the caller's to supply.
template <binding_traits T>
[[nodiscard]] auto read_attributes(const headers& fields) -> result<event> {
  event cloud_event{.id = {}, .source = {}, .type = {}};
  bool saw_id = false;
  bool saw_source = false;
  bool saw_type = false;
  result<void> header_error{};

  for (const auto& [name, raw_value] : fields) {
    if (!detail::carries_prefix<T>(name)) {
      continue;
    }
    const std::string attribute = detail::attribute_name_of<T>(name);

    auto decoded = T::decode_value(raw_value);
    if (!decoded) {
      header_error = fail(decoded.error().code, decoded.error().detail, attribute);
      break;
    }

    // Hoisted out of the chain below rather than joined to it with &&: mixing a
    // compile-time constant into a runtime condition is C4127 under MSVC's /W4,
    // and `if constexpr` is what actually expresses "this branch does not exist
    // for that binding".
    if constexpr (detail::content_type_policy<T>::as_attribute) {
      if (attribute == "datacontenttype") {
        cloud_event.datacontenttype = *decoded;
        continue;
      }
    } else {
      // Where the binding carries the media type in its own content-type field,
      // a prefixed datacontenttype is a field the binding does not define. It
      // used to fall through to the extension branch, which accepted the name,
      // stored it, and left validate() to refuse it later as a reserved name -
      // a complaint about the name rather than about where it arrived.
      if (attribute == "datacontenttype") {
        header_error = fail(errc::invalid_argument,
                            "this binding carries datacontenttype in its content-type field, "
                            "not as a prefixed attribute",
                            attribute);
        break;
      }
    }

    if (attribute == "specversion") {
      cloud_event.specversion = *decoded;
    } else if (attribute == "id") {
      cloud_event.id = *decoded;
      saw_id = true;
    } else if (attribute == "source") {
      cloud_event.source = uri_ref{*decoded};
      saw_source = true;
    } else if (attribute == "type") {
      cloud_event.type = *decoded;
      saw_type = true;
    } else if (attribute == "dataschema") {
      cloud_event.dataschema = uri{*decoded};
    } else if (attribute == "subject") {
      cloud_event.subject = *decoded;
    } else if (attribute == "time") {
      auto parsed = parse_timestamp(*decoded);
      if (!parsed) {
        header_error = fail(parsed.error().code, parsed.error().detail, "time");
        break;
      }
      cloud_event.time = *parsed;
    } else {
      // Same rule as the JSON format: a prefixed field whose name is not one the
      // spec allows cannot become an extension, or reading would return an event
      // that writing then refuses.
      if (!valid_attribute_name(attribute)) {
        header_error =
            fail(errc::invalid_attribute_name, "extension names must match [a-z0-9]+", attribute);
        break;
      }
      // The wire form carries no type, so an extension arrives as a string. The
      // typed extension structs are what recover the declared type.
      cloud_event.extensions.insert_or_assign(attribute, attribute_value{*decoded});
    }
  }

  if (!header_error) {
    return fail(header_error.error().code, header_error.error().detail, header_error.error().where);
  }
  if (cloud_event.specversion != "1.0") {
    return fail(errc::unsupported_spec_version, "this SDK implements CloudEvents 1.0 only",
                "specversion");
  }
  if (!saw_id || !saw_source || !saw_type) {
    return fail(errc::missing_required_attribute, "id, source and type are all required",
                !saw_id ? "id" : (!saw_source ? "source" : "type"));
  }
  return cloud_event;
}

/// \brief The payload, as the message body.
inline void write_body(const event& cloud_event, message& into) {
  std::visit(
      [&into](const auto& held) {
        using held_type = std::remove_cvref_t<decltype(held)>;
        if constexpr (std::is_same_v<held_type, std::monostate>) {
          // no body
        } else if constexpr (std::is_same_v<held_type, binary>) {
          into.body = held;
        } else if constexpr (std::is_same_v<held_type, std::string>) {
          into.body = to_bytes(held);
        } else {
          into.body = to_bytes(held.raw);
        }
      },
      cloud_event.data);
}

/// \brief The message body, as the payload. Reads `into.datacontenttype`, so the
/// caller sets that first.
inline void read_body(const binary& body, event& into) {
  if (body.empty()) {
    return;
  }
  if (into.datacontenttype && is_json_content_type(*into.datacontenttype)) {
    into.data = json_text{.raw = to_text(body)};
  } else {
    into.data = body;
  }
}

/// \brief The whole event as one document in the body, under the event content type.
template <binding_traits T, json::json_codec Codec>
[[nodiscard]] auto encode_structured(const event& cloud_event) -> result<message> {
  auto text = json_format<Codec>::encode(cloud_event);
  if (!text) {
    return fail(text.error().code, text.error().detail, text.error().where);
  }
  message out;
  detail::put<T>(out.header_fields, std::string{T::content_type_header},
                 std::string{json::content_type});
  out.body = to_bytes(*text);
  return out;
}

/// \brief The body, read back as one event.
template <json::json_codec Codec>
[[nodiscard]] auto decode_structured(const message& from) -> result<event> {
  return json_format<Codec>::decode(to_text(from.body));
}

}  // namespace ce::inline v1::binding
