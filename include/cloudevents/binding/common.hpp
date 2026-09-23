#pragma once

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

// spec: SYS-BIND-0001
namespace ce::inline v2::binding {

// spec: SWR-BIND-0001
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

// spec: SWR-BIND-0003
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
    return ce::v2::detail::starts_with_ignoring_case(name, T::attribute_prefix);
  }
}

template <binding_traits T>
[[nodiscard]] auto attribute_name_of(std::string_view field) -> std::string {
  std::string name{field.substr(std::string_view{T::attribute_prefix}.size())};
  if constexpr (!T::case_sensitive_names) {
    for (char& character : name) {
      character = ce::v2::detail::ascii_lower(character);
    }
  }
  return name;
}

// spec: SWR-BIND-0005
template <binding_traits T>
[[nodiscard]] auto apply_attribute(event::builder& into, std::string attribute, std::string value)
    -> result<void> {
  if (attribute == "datacontenttype") {
    if constexpr (content_type_policy<T>::as_attribute) {
      return ce::v2::detail::store_attribute(into.rest.datacontenttype, std::move(value));
    } else {
      return fail(errc::invalid_argument,
                  "this binding carries datacontenttype in its content-type field, "
                  "not as a prefixed attribute",
                  std::move(attribute));
    }
  }
  if (attribute == "specversion") {
    if (auto version = spec_version::make(value); !version) {
      return fail(version.error().code, version.error().detail, version.error().where);
    }
    return {};
  }
  if (attribute == "id") {
    return ce::v2::detail::store_attribute(into.id, std::move(value));
  }
  if (attribute == "source") {
    return ce::v2::detail::store_attribute(into.source, std::move(value));
  }
  if (attribute == "type") {
    return ce::v2::detail::store_attribute(into.type, std::move(value));
  }
  if (attribute == "dataschema") {
    return ce::v2::detail::store_attribute(into.rest.dataschema, std::move(value));
  }
  if (attribute == "subject") {
    return ce::v2::detail::store_attribute(into.rest.subject, std::move(value));
  }
  if (attribute == "time") {
    auto parsed = parse_timestamp(value);
    if (!parsed) {
      return fail(parsed.error().code, parsed.error().detail, "time");
    }
    into.rest.time = *parsed;
    return {};
  }
  auto extension = extension_name::make(std::move(attribute));
  if (!extension) {
    return fail(extension.error().code, extension.error().detail, extension.error().where);
  }
  into.rest.extensions.insert_or_assign(std::move(*extension), attribute_value{std::move(value)});
  return {};
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

// spec: SWR-BIND-0002
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

  put_attribute("specversion", spec_version::view());
  put_attribute("id", cloud_event.id().view());
  put_attribute("source", cloud_event.source().view());
  put_attribute("type", cloud_event.type().view());
  if (const auto& schema = cloud_event.dataschema(); schema) {
    put_attribute("dataschema", schema->view());
  }
  if (const auto& named = cloud_event.subject(); named) {
    put_attribute("subject", named->view());
  }
  if (const auto& when = cloud_event.time(); when) {
    put_attribute("time", to_string(*when));
  }
  for (const auto& [name, attribute] : cloud_event.extensions()) {
    put_attribute(name.view(), render_attribute(attribute));
  }

  if (!failure) {
    return failure;
  }

  if (const auto& media_type = cloud_event.datacontenttype(); media_type) {
    if constexpr (detail::content_type_policy<T>::as_attribute) {
      put_attribute("datacontenttype", media_type->view());
    } else {
      detail::put<T>(into, std::string{T::content_type_header}, media_type->str());
    }
  }
  return failure;
}

template <binding_traits T>
[[nodiscard]] auto read_attributes(const raw_headers& delivered) -> result<event::builder> {
  auto adopted = headers::adopt(delivered, T::case_sensitive_names
                                               ? name_matching::case_sensitive
                                               : name_matching::case_insensitive);
  if (!adopted) {
    return fail(adopted.error().code, adopted.error().detail, adopted.error().where);
  }
  const headers& fields = *adopted;

  event::builder under_construction{};
  for (const auto& [name, raw_value] : fields) {
    if (!detail::carries_prefix<T>(name)) {
      continue;
    }
    std::string attribute = detail::attribute_name_of<T>(name);

    auto decoded = T::decode_value(raw_value);
    if (!decoded) {
      return fail(decoded.error().code, decoded.error().detail, attribute);
    }
    if (auto applied = detail::apply_attribute<T>(under_construction, std::move(attribute),
                                                  std::move(*decoded));
        !applied) {
      return fail(applied.error().code, applied.error().detail, applied.error().where);
    }
  }
  return under_construction;
}

inline void write_body(const event& cloud_event, message& into) {
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
      cloud_event.data());
}

// spec: SWR-BIND-0006
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

template <json::json_codec Codec>
[[nodiscard]] auto decode_structured(const message& from) -> result<event> {
  return json_format<Codec>::decode(to_text(from.body));
}

}  // namespace ce::inline v2::binding
