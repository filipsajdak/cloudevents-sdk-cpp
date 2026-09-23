#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <cloudevents/v1/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/v1/format/json_format.hpp>
#include <cloudevents/v1/message.hpp>
#include <cloudevents/result.hpp>

namespace ce::v1::binding {

template <class T>
concept binding_traits = requires(std::string_view text) {
  { T::attribute_prefix } -> std::convertible_to<std::string_view>;
  { T::content_type_header } -> std::convertible_to<std::string_view>;
  { T::case_sensitive_names } -> std::convertible_to<bool>;
  { T::encode_value(text) } -> std::same_as<result<std::string>>;
  { T::decode_value(text) } -> std::same_as<result<std::string>>;
};

namespace detail {

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

template <binding_traits T>
[[nodiscard]] auto write_attributes(const event& subject, headers& into) -> result<void> {
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

  put_attribute("specversion", subject.specversion);
  put_attribute("id", subject.id);
  put_attribute("source", subject.source.view());
  put_attribute("type", subject.type);
  if (subject.dataschema) {
    put_attribute("dataschema", subject.dataschema->view());
  }
  if (subject.subject) {
    put_attribute("subject", *subject.subject);
  }
  if (subject.time) {
    put_attribute("time", to_string(*subject.time));
  }
  for (const auto& [name, attribute] : subject.extensions) {
    put_attribute(name, render_attribute(attribute));
  }

  if (!failure) {
    return failure;
  }

  if (subject.datacontenttype) {
    if constexpr (detail::content_type_policy<T>::as_attribute) {
      put_attribute("datacontenttype", *subject.datacontenttype);
    } else {
      detail::put<T>(into, std::string{T::content_type_header}, *subject.datacontenttype);
    }
  }
  return failure;
}

template <binding_traits T>
[[nodiscard]] auto read_attributes(const headers& fields) -> result<event> {
  event subject{.id = {}, .source = {}, .type = {}};
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

    if constexpr (detail::content_type_policy<T>::as_attribute) {
      if (attribute == "datacontenttype") {
        subject.datacontenttype = *decoded;
        continue;
      }
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
      if (!valid_attribute_name(attribute)) {
        header_error =
            fail(errc::invalid_attribute_name, "extension names must match [a-z0-9]+", attribute);
        break;
      }
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
  return subject;
}

inline void write_body(const event& subject, message& into) {
  std::visit(
      [&into](const auto& held) {
        using held_type = std::remove_cvref_t<decltype(held)>;
        if constexpr (std::is_same_v<held_type, std::monostate>) {
        } else if constexpr (std::is_same_v<held_type, binary>) {
          into.body = held;
        } else if constexpr (std::is_same_v<held_type, std::string>) {
          into.body = to_bytes(held);
        } else {
          into.body = to_bytes(held.raw);
        }
      },
      subject.data);
}

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

template <binding_traits T, json::json_codec Codec>
[[nodiscard]] auto encode_structured(const event& subject) -> result<message> {
  auto text = json_format<Codec>::encode(subject);
  if (!text) {
    return fail(text.error().code, text.error().detail, text.error().where);
  }
  message out;
  detail::put<T>(out.header_fields, std::string{T::content_type_header},
                 std::string{json::content_type});
  out.body = to_bytes(*text);
  return out;
}

template <json::json_codec Codec>
[[nodiscard]] auto decode_structured(const message& from) -> result<event> {
  return json_format<Codec>::decode(to_text(from.body));
}

}  // namespace ce::v1::binding
