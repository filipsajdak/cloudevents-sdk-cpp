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
void put(raw_headers& into, std::string name, std::string value) {
  if constexpr (T::case_sensitive_names) {
    into.set_exact(std::move(name), std::move(value));
  } else {
    into.set(std::move(name), std::move(value));
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
[[nodiscard]] auto write_attributes(const event& cloud_event, raw_headers& into) -> result<void> {
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
    detail::put<T>(into, std::string{T::attribute_prefix}.append(name), std::move(*encoded));
  };

  put_attribute("specversion", spec_version_of(cloud_event).view());
  put_attribute("id", id_of(cloud_event).view());
  put_attribute("source", source_of(cloud_event).view());
  put_attribute("type", type_of(cloud_event).view());
  // Bound once rather than called twice. Two calls are two expressions as far
  // as a reader or a checker is concerned, and nothing says the second yields
  // the engaged optional the first one tested.
  if (const auto& schema = dataschema_of(cloud_event); schema) {
    put_attribute("dataschema", schema->view());
  }
  if (const auto& named = subject_of(cloud_event); named) {
    put_attribute("subject", named->view());
  }
  if (const auto& when = time_of(cloud_event); when) {
    put_attribute("time", to_string(*when));
  }
  for (const auto& [name, attribute] : extensions_of(cloud_event)) {
    put_attribute(name.view(), render_attribute(attribute));
  }

  if (!failure) {
    return failure;
  }

  // Where datacontenttype travels as the content-type field it must not also
  // appear under the prefix, or a receiver sees the same attribute twice, and it
  // is not encoded there: it is a media type, not an attribute value. Where the
  // binding maps it like any other attribute, it is encoded like one.
  if (const auto& media_type = datacontenttype_of(cloud_event); media_type) {
    if constexpr (detail::content_type_policy<T>::as_attribute) {
      put_attribute("datacontenttype", media_type->view());
    } else {
      detail::put<T>(into, std::string{T::content_type_header}, media_type->str());
    }
  }
  return failure;
}

/// \brief Read the attributes out of a message's fields.
///
/// Hands back the builder rather than an event, because the payload and the
/// media type describing it come from the body, which is the caller's to supply.
/// The event is built once everything is known, so there is no window in which
/// one exists without its payload (SWR-BIND-0006).
template <binding_traits T>
[[nodiscard]] auto read_attributes(const raw_headers& delivered) -> result<event::builder> {
  // The ingress gate. `raw_headers::find` returns the first field of a name
  // while the loop below lets the last one win, so a message carrying `ce-id`
  // twice decoded differently from how the content mode was detected. Refusing
  // it here removes the disagreement rather than picking a winner (SWR-MSG-0004).
  auto adopted = headers::adopt(delivered, T::case_sensitive_names
                                               ? name_matching::case_sensitive
                                               : name_matching::case_insensitive);
  if (!adopted) {
    return fail(adopted.error().code, adopted.error().detail, adopted.error().where);
  }
  const headers& fields = *adopted;

  event::builder under_construction{};
  result<void> header_error{};

  // Every context attribute is made the same way: the wire text goes to the
  // attribute's own factory, which is the only thing that can refuse it.
  const auto store = [&header_error]<class Attribute>(std::optional<Attribute>& slot,
                                                      std::string text) {
    auto made = Attribute::make(std::move(text));
    if (!made) {
      header_error = fail(made.error().code, made.error().detail, made.error().where);
      return false;
    }
    slot = std::move(*made);
    return true;
  };

  for (const auto& [name, raw_value] : fields) {
    if (!detail::carries_prefix<T>(name)) {
      continue;
    }
    std::string attribute = detail::attribute_name_of<T>(name);

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
        if (!store(under_construction.rest.datacontenttype, std::move(*decoded))) {
          break;
        }
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
      auto version = spec_version::make(*decoded);
      if (!version) {
        header_error = fail(version.error().code, version.error().detail, version.error().where);
        break;
      }
    } else if (attribute == "id") {
      if (!store(under_construction.id, std::move(*decoded))) {
        break;
      }
    } else if (attribute == "source") {
      if (!store(under_construction.source, std::move(*decoded))) {
        break;
      }
    } else if (attribute == "type") {
      if (!store(under_construction.type, std::move(*decoded))) {
        break;
      }
    } else if (attribute == "dataschema") {
      if (!store(under_construction.rest.dataschema, std::move(*decoded))) {
        break;
      }
    } else if (attribute == "subject") {
      if (!store(under_construction.rest.subject, std::move(*decoded))) {
        break;
      }
    } else if (attribute == "time") {
      auto parsed = parse_timestamp(*decoded);
      if (!parsed) {
        header_error = fail(parsed.error().code, parsed.error().detail, "time");
        break;
      }
      under_construction.rest.time = *parsed;
    } else {
      // Same rule as the JSON format: a prefixed field whose name is not one the
      // spec allows cannot become an extension, or reading would return an event
      // that writing then refuses. `extension_name` is where that rule lives now.
      auto extension = extension_name::make(std::move(attribute));
      if (!extension) {
        header_error =
            fail(extension.error().code, extension.error().detail, extension.error().where);
        break;
      }
      // The wire form carries no type, so an extension arrives as a string. The
      // typed extension structs are what recover the declared type.
      under_construction.rest.extensions.insert_or_assign(std::move(*extension),
                                                          attribute_value{std::move(*decoded)});
    }
  }

  if (!header_error) {
    return fail(header_error.error().code, header_error.error().detail, header_error.error().where);
  }
  return under_construction;
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
      data_of(cloud_event));
}

/// \brief The message body, as the payload.
///
/// Takes the media type rather than reading it back off a half-built event, so
/// the ordering the old out-parameter form required cannot be got wrong.
[[nodiscard]] inline auto read_body(const binary& body,
                                    const std::optional<datacontenttype>& media_type) -> data_t {
  if (body.empty()) {
    return {};
  }
  if (media_type && is_json_content_type(media_type->view())) {
    return json_text{.raw = to_text(body)};
  }
  return body;
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
