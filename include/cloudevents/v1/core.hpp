#pragma once

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#include <ctre.hpp>

#include <cloudevents/describe.hpp>
#include <cloudevents/detail/config.hpp>
#include <cloudevents/v1/detail/timestamp.hpp>
#include <cloudevents/result.hpp>

// spec: SWR-BUILD-0011
namespace ce::v1 {

using binary = std::vector<std::byte>;

namespace detail {

template <class Tag>
class tagged_string {
 public:
  tagged_string() = default;

  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  tagged_string(std::string text) : text_{std::move(text)} {}
  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  tagged_string(const char* text) : text_{text} {}

  [[nodiscard]] auto str() const noexcept -> const std::string& { return text_; }
  [[nodiscard]] auto view() const noexcept -> std::string_view { return text_; }
  [[nodiscard]] auto empty() const noexcept -> bool { return text_.empty(); }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return text_.size(); }

  friend auto operator==(const tagged_string&, const tagged_string&) -> bool = default;

 private:
  std::string text_;
};

struct uri_tag {};
struct uri_ref_tag {};

}  // namespace detail

using uri = detail::tagged_string<detail::uri_tag>;

using uri_ref = detail::tagged_string<detail::uri_ref_tag>;

using attribute_value =
    std::variant<bool, std::int32_t, std::string, binary, uri, uri_ref, timestamp>;

struct json_text {
  std::string raw;

  friend auto operator==(const json_text&, const json_text&) -> bool = default;
};

using data_t = std::variant<std::monostate, std::string, binary, json_text>;

namespace detail {

inline constexpr auto attribute_name_pattern = ctll::fixed_string{R"(^[a-z0-9]+$)"};

inline constexpr auto content_type_pattern =
    ctll::fixed_string{R"(^\s*([A-Za-z0-9!#$%&'*+.^_`|~\-]+)/([A-Za-z0-9!#$%&'*+.^_`|~\-]+)\s*(;.*)?$)"};

inline constexpr std::string_view reserved_names[] = {
    "id",      "source",  "specversion", "type",    "datacontenttype",
    "dataschema", "subject", "time",     "data",    "data_base64",
};

[[nodiscard]] constexpr auto ascii_lower(char character) noexcept -> char {
  return (character >= 'A' && character <= 'Z')
             ? static_cast<char>(character - 'A' + 'a')
             : character;
}

[[nodiscard]] constexpr auto iequals(std::string_view left, std::string_view right) noexcept
    -> bool {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.size(); ++i) {
    if (ascii_lower(left[i]) != ascii_lower(right[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr auto iends_with(std::string_view text, std::string_view suffix) noexcept
    -> bool {
  return text.size() >= suffix.size() && iequals(text.substr(text.size() - suffix.size()), suffix);
}

[[nodiscard]] constexpr auto starts_with_ignoring_case(std::string_view text,
                                                       std::string_view prefix) noexcept -> bool {
  return text.size() >= prefix.size() &&
         ce::v1::detail::iequals(text.substr(0, prefix.size()), prefix);
}

[[nodiscard]] constexpr auto is_valid_utf8(std::string_view text) noexcept -> bool {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t length = 0;
    std::uint32_t code = 0;

    if (lead < 0x80) {
      ++index;
      continue;
    }
    if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
      code = lead & 0x1FU;
    } else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
      code = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
      code = lead & 0x07U;
    } else {
      return false;
    }

    if (index + length > text.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset < length; ++offset) {
      const auto continuation = static_cast<unsigned char>(text[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        return false;
      }
      code = (code << 6U) | (continuation & 0x3FU);
    }

    if (length == 2 && code < 0x80) {
      return false;
    }
    if (length == 3 && code < 0x800) {
      return false;
    }
    if (length == 4 && code < 0x10000) {
      return false;
    }
    if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
      return false;
    }
    index += length;
  }
  return true;
}

}  // namespace detail

[[nodiscard]] constexpr auto valid_attribute_name(std::string_view name) noexcept -> bool {
  return ctre::match<detail::attribute_name_pattern>(name);
}

[[nodiscard]] constexpr auto reserved_name(std::string_view name) noexcept -> bool {
  for (const auto reserved : detail::reserved_names) {
    if (reserved == name) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] constexpr auto is_json_content_type(std::string_view content_type) noexcept -> bool {
  const auto match = ctre::match<detail::content_type_pattern>(content_type);
  if (!match) {
    return false;
  }
  const auto subtype = match.get<2>().to_view();
  return detail::iequals(subtype, "json") || detail::iends_with(subtype, "+json");
}

namespace detail {

template <class F>
struct extension_field_type : std::false_type {};

template <>
struct extension_field_type<bool> : std::true_type {};
template <>
struct extension_field_type<std::int32_t> : std::true_type {};
template <>
struct extension_field_type<std::string> : std::true_type {};
template <>
struct extension_field_type<uri> : std::true_type {};
template <>
struct extension_field_type<uri_ref> : std::true_type {};
template <>
struct extension_field_type<timestamp> : std::true_type {};

template <class U>
struct extension_field_type<std::optional<U>> : extension_field_type<U> {};

template <class F>
concept extension_field = extension_field_type<std::remove_cvref_t<F>>::value;

template <class F>
struct extension_value_type {
  using type = F;
};
template <class U>
struct extension_value_type<std::optional<U>> {
  using type = U;
};
template <class F>
using extension_value_t = typename extension_value_type<std::remove_cvref_t<F>>::type;

template <class F>
inline constexpr bool is_optional_field = false;
template <class U>
inline constexpr bool is_optional_field<std::optional<U>> = true;

template <extension_field F>
[[nodiscard]] auto from_attribute_text(std::string_view text, std::string_view where)
    -> result<extension_value_t<F>> {
  using value_type = extension_value_t<F>;

  if constexpr (std::is_same_v<value_type, std::string>) {
    return std::string{text};
  } else if constexpr (std::is_same_v<value_type, uri>) {
    return uri{std::string{text}};
  } else if constexpr (std::is_same_v<value_type, uri_ref>) {
    return uri_ref{std::string{text}};
  } else if constexpr (std::is_same_v<value_type, bool>) {
    if (text == "true") {
      return true;
    }
    if (text == "false") {
      return false;
    }
    return fail(errc::type_mismatch, "expected \"true\" or \"false\"", std::string{where});
  } else if constexpr (std::is_same_v<value_type, std::int32_t>) {
    std::int32_t parsed = 0;
    const char* const first = text.data();
    const char* const last = first + text.size();
    const auto [stop, code] = std::from_chars(first, last, parsed);
    if (code != std::errc{} || stop != last) {
      return fail(errc::type_mismatch, "expected an integer", std::string{where});
    }
    return parsed;
  } else {
    auto parsed = parse_timestamp(text);
    if (!parsed) {
      return fail(errc::type_mismatch, parsed.error().detail, std::string{where});
    }
    return *parsed;
  }
}

template <described Ext>
[[nodiscard]] consteval auto extension_fields_supported() -> bool {
  bool supported = true;
  Ext probe{};
  for_each_field(probe, [&supported](std::string_view, auto& field) {
    supported = supported && extension_field<std::remove_cvref_t<decltype(field)>>;
  });
  return supported;
}

template <extension_field F>
[[nodiscard]] auto read_attribute(const attribute_value& stored, std::string_view where)
    -> result<extension_value_t<F>> {
  using value_type = extension_value_t<F>;

  if (const auto* exact = std::get_if<value_type>(&stored)) {
    return *exact;
  }
  if (const auto* text = std::get_if<std::string>(&stored)) {
    return from_attribute_text<F>(*text, where);
  }
  return fail(errc::type_mismatch, "the stored attribute has an unrelated type",
              std::string{where});
}

}  // namespace detail

[[nodiscard]] inline auto to_bytes(std::string_view text) -> binary {
  binary out;
  out.reserve(text.size());
  for (const char character : text) {
    out.push_back(static_cast<std::byte>(character));
  }
  return out;
}

[[nodiscard]] inline auto to_text(const binary& bytes) -> std::string {
  std::string out;
  out.reserve(bytes.size());
  for (const std::byte value : bytes) {
    out.push_back(static_cast<char>(value));
  }
  return out;
}

struct lint_warning {
  std::string attribute;
  std::string message;

  friend auto operator==(const lint_warning&, const lint_warning&) -> bool = default;
};

struct event {
  std::string id;
  uri_ref source;
  std::string type;

  std::string specversion = "1.0";
  std::optional<std::string> datacontenttype = {};
  std::optional<uri> dataschema = {};
  std::optional<std::string> subject = {};
  std::optional<timestamp> time = {};

  std::map<std::string, attribute_value, std::less<>> extensions = {};

  data_t data = {};

  friend auto operator==(const event&, const event&) -> bool = default;

  auto set_extension(std::string name, attribute_value value) -> result<void> {
    if (!valid_attribute_name(name)) {
      return fail(errc::invalid_attribute_name,
                  "extension names must match [a-z0-9]+", name);
    }
    if (reserved_name(name)) {
      return fail(errc::reserved_attribute_name,
                  "an extension may not redefine a context attribute", name);
    }
    extensions.insert_or_assign(std::move(name), std::move(value));
    return {};
  }

  [[nodiscard]] auto extension(std::string_view name) const noexcept -> const attribute_value* {
    const auto found = extensions.find(name);
    return found == extensions.end() ? nullptr : &found->second;
  }

  template <described Ext>
  [[nodiscard]] auto get() const -> result<Ext> {
    static_assert(detail::extension_fields_supported<Ext>(),
                  "an extension struct may only declare bool, int32_t, std::string, uri, "
                  "uri_ref or timestamp fields, optionally wrapped in std::optional");

    Ext out{};
    result<void> mapping_error{};

    for_each_field(out, [this, &mapping_error](std::string_view name, auto& field) {
      using field_type = std::remove_cvref_t<decltype(field)>;
      if (!mapping_error) {
        return;
      }
      const attribute_value* stored = extension(name);
      if (stored == nullptr) {
        if constexpr (!detail::is_optional_field<field_type>) {
          mapping_error = fail(errc::missing_required_attribute,
                         "the extension requires this attribute", std::string{name});
        }
        return;
      }
      auto read = detail::read_attribute<field_type>(*stored, name);
      if (!read) {
        mapping_error = fail(read.error().code, read.error().detail, read.error().where);
        return;
      }
      field = std::move(*read);
    });

    if (!mapping_error) {
      return fail(mapping_error.error().code, mapping_error.error().detail, mapping_error.error().where);
    }
    return out;
  }

  template <described Ext>
  auto set(const Ext& value) -> result<void> {
    static_assert(detail::extension_fields_supported<Ext>(),
                  "an extension struct may only declare bool, int32_t, std::string, uri, "
                  "uri_ref or timestamp fields, optionally wrapped in std::optional");

    result<void> mapping_error{};
    for_each_field(value, [this, &mapping_error](std::string_view name, const auto& field) {
      using field_type = std::remove_cvref_t<decltype(field)>;
      if (!mapping_error) {
        return;
      }
      if constexpr (detail::is_optional_field<field_type>) {
        if (!field) {
          extensions.erase(std::string{name});
          return;
        }
        mapping_error = set_extension(std::string{name}, attribute_value{*field});
      } else {
        mapping_error = set_extension(std::string{name}, attribute_value{field});
      }
    });
    return mapping_error;
  }

  [[nodiscard]] auto validate() const -> result<void> {
    if (specversion != "1.0") {
      return fail(errc::unsupported_spec_version,
                  "this SDK implements CloudEvents 1.0 only", "specversion");
    }
    if (id.empty()) {
      return fail(errc::missing_required_attribute, "id must be non-empty", "id");
    }
    if (source.empty()) {
      return fail(errc::missing_required_attribute, "source must be non-empty", "source");
    }
    if (type.empty()) {
      return fail(errc::missing_required_attribute, "type must be non-empty", "type");
    }

    if (datacontenttype && datacontenttype->empty()) {
      return fail(errc::invalid_attribute_value,
                  "datacontenttype is present but empty", "datacontenttype");
    }
    if (datacontenttype && !is_json_content_type(*datacontenttype) &&
        !ctre::match<detail::content_type_pattern>(*datacontenttype)) {
      return fail(errc::invalid_content_type, "not a valid media type", "datacontenttype");
    }
    if (dataschema && dataschema->empty()) {
      return fail(errc::invalid_attribute_value, "dataschema is present but empty", "dataschema");
    }
    if (subject && subject->empty()) {
      return fail(errc::invalid_attribute_value, "subject is present but empty", "subject");
    }

    for (const auto& [name, value] : extensions) {
      if (!valid_attribute_name(name)) {
        return fail(errc::invalid_attribute_name,
                    "extension names must match [a-z0-9]+", name);
      }
      if (reserved_name(name)) {
        return fail(errc::reserved_attribute_name,
                    "an extension may not redefine a context attribute", name);
      }
    }

    return {};
  }

  [[nodiscard]] auto lint() const -> std::vector<lint_warning> {
    std::vector<lint_warning> warnings;
    constexpr std::size_t recommended_name_length = 20;
    for (const auto& [name, value] : extensions) {
      if (name.size() > recommended_name_length) {
        warnings.push_back(lint_warning{
            .attribute = name,
            .message = "extension names should be 20 characters or fewer",
        });
      }
    }
    return warnings;
  }
};

}  // namespace ce::v1
