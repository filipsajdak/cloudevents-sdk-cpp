#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#include <ctre.hpp>

#include <cloudevents/describe.hpp>
#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/timestamp.hpp>
#include <cloudevents/detail/validated_string.hpp>
#include <cloudevents/result.hpp>

// spec: SYS-CORE-0001
// spec: SWR-BUILD-0005
namespace ce::inline v2 {

// spec: SWR-CORE-0005
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

// spec: SWR-CORE-0006
using attribute_value =
    std::variant<bool, std::int32_t, std::string, binary, uri, uri_ref, timestamp>;

// spec: SWR-CORE-0013
struct json_text {
  std::string raw;

  friend auto operator==(const json_text&, const json_text&) -> bool = default;
};

namespace detail {

inline constexpr auto attribute_name_pattern = ctll::fixed_string{R"(^[a-z0-9]+$)"};

inline constexpr auto content_type_pattern =
    ctll::fixed_string{R"(^\s*([A-Za-z0-9!#$%&'*+.^_`|~\-]+)/([A-Za-z0-9!#$%&'*+.^_`|~\-]+)\s*(;.*)?$)"};

inline constexpr std::array<std::string_view, 10> reserved_names = {
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
  return std::ranges::equal(left, right, {}, ascii_lower, ascii_lower);
}

[[nodiscard]] constexpr auto iends_with(std::string_view text, std::string_view suffix) noexcept
    -> bool {
  return text.size() >= suffix.size() &&
         std::ranges::equal(
             text | std::views::drop(static_cast<std::ptrdiff_t>(text.size() - suffix.size())),
             suffix, {}, ascii_lower, ascii_lower);
}

[[nodiscard]] constexpr auto starts_with_ignoring_case(std::string_view text,
                                                       std::string_view prefix) noexcept -> bool {
  return text.size() >= prefix.size() &&
         std::ranges::equal(text | std::views::take(static_cast<std::ptrdiff_t>(prefix.size())),
                            prefix, {}, ascii_lower, ascii_lower);
}

namespace utf8 {

inline constexpr unsigned char ascii_limit = 0x80U;

inline constexpr unsigned char continuation_mask = 0xC0U;
inline constexpr unsigned char continuation_marker = 0x80U;
inline constexpr std::uint32_t continuation_payload = 0x3FU;
inline constexpr unsigned int continuation_bits = 6U;

inline constexpr unsigned char two_byte_mask = 0xE0U;
inline constexpr unsigned char two_byte_marker = 0xC0U;
inline constexpr std::uint32_t two_byte_payload = 0x1FU;

inline constexpr unsigned char three_byte_mask = 0xF0U;
inline constexpr unsigned char three_byte_marker = 0xE0U;
inline constexpr std::uint32_t three_byte_payload = 0x0FU;

inline constexpr unsigned char four_byte_mask = 0xF8U;
inline constexpr unsigned char four_byte_marker = 0xF0U;
inline constexpr std::uint32_t four_byte_payload = 0x07U;

inline constexpr std::uint32_t two_byte_floor = 0x80U;
inline constexpr std::uint32_t three_byte_floor = 0x800U;
inline constexpr std::uint32_t four_byte_floor = 0x10000U;

inline constexpr std::uint32_t first_surrogate = 0xD800U;
inline constexpr std::uint32_t last_surrogate = 0xDFFFU;

inline constexpr std::uint32_t last_code_point = 0x10FFFFU;

}  // namespace utf8

struct utf8_lead {
  std::size_t length;
  std::uint32_t payload;
};

[[nodiscard]] constexpr auto read_utf8_lead(unsigned char lead) noexcept -> utf8_lead {
  if (lead < utf8::ascii_limit) {
    return {.length = 1, .payload = lead};
  }
  if ((lead & utf8::two_byte_mask) == utf8::two_byte_marker) {
    return {.length = 2, .payload = lead & utf8::two_byte_payload};
  }
  if ((lead & utf8::three_byte_mask) == utf8::three_byte_marker) {
    return {.length = 3, .payload = lead & utf8::three_byte_payload};
  }
  if ((lead & utf8::four_byte_mask) == utf8::four_byte_marker) {
    return {.length = 4, .payload = lead & utf8::four_byte_payload};
  }
  return {.length = 0, .payload = 0};
}

[[nodiscard]] constexpr auto utf8_floor(std::size_t length) noexcept -> std::uint32_t {
  switch (length) {
    case 2:
      return utf8::two_byte_floor;
    case 3:
      return utf8::three_byte_floor;
    default:
      return utf8::four_byte_floor;
  }
}

[[nodiscard]] constexpr auto utf8_code_point_allowed(std::size_t length, std::uint32_t code) noexcept
    -> bool {
  return code >= utf8_floor(length) && code <= utf8::last_code_point &&
         (code < utf8::first_surrogate || code > utf8::last_surrogate);
}

[[nodiscard]] constexpr auto is_valid_utf8(std::string_view text) noexcept -> bool {
  while (!text.empty()) {
    const auto [length, payload] = read_utf8_lead(static_cast<unsigned char>(text.front()));
    if (length == 0 || length > text.size()) {
      return false;
    }
    std::uint32_t code = payload;
    const auto continuations =
        text | std::views::drop(1) | std::views::take(static_cast<std::ptrdiff_t>(length - 1));
    for (const char byte : continuations) {
      const auto continuation = static_cast<unsigned char>(byte);
      if ((continuation & utf8::continuation_mask) != utf8::continuation_marker) {
        return false;
      }
      code = (code << utf8::continuation_bits) | (continuation & utf8::continuation_payload);
    }
    if (length > 1 && !utf8_code_point_allowed(length, code)) {
      return false;
    }
    text.remove_prefix(length);
  }
  return true;
}

}  // namespace detail

// spec: SWR-CORE-0022
[[nodiscard]] constexpr auto valid_attribute_name(std::string_view name) noexcept -> bool {
  return ctre::match<detail::attribute_name_pattern>(name);
}

// spec: SWR-CORE-0023
[[nodiscard]] constexpr auto reserved_name(std::string_view name) noexcept -> bool {
  return std::ranges::find(detail::reserved_names, name) != detail::reserved_names.end();
}

// spec: SWR-CORE-0024
[[nodiscard]] constexpr auto is_json_content_type(std::string_view content_type) noexcept -> bool {
  const auto match = ctre::match<detail::content_type_pattern>(content_type);
  if (!match) {
    return false;
  }
  const auto subtype = match.get<2>().to_view();
  return detail::iequals(subtype, "json") || detail::iends_with(subtype, "+json");
}

namespace detail {

// spec: SWR-CORE-0025
template <errc Empty>
[[nodiscard]] constexpr auto check_text(std::string_view text, std::string_view attribute,
                                        std::string_view empty_detail) noexcept
    -> std::optional<static_error> {
  if (text.empty()) {
    return static_error{.code = Empty, .detail = empty_detail, .where = attribute};
  }
  if (!is_valid_utf8(text)) {
    return static_error{
        .code = errc::invalid_utf8,
        .detail = "an attribute value must be well-formed UTF-8",
        .where = attribute,
    };
  }
  return {};
}

// spec: SWR-CORE-0017
struct id_policy {
  static constexpr std::string_view attribute = "id";
  [[nodiscard]] static constexpr auto check(std::string_view text) noexcept
      -> std::optional<static_error> {
    return check_text<errc::missing_required_attribute>(text, attribute, "id must be non-empty");
  }
};

struct source_policy {
  static constexpr std::string_view attribute = "source";
  [[nodiscard]] static constexpr auto check(std::string_view text) noexcept
      -> std::optional<static_error> {
    return check_text<errc::missing_required_attribute>(text, attribute,
                                                        "source must be non-empty");
  }
};

struct type_policy {
  static constexpr std::string_view attribute = "type";
  [[nodiscard]] static constexpr auto check(std::string_view text) noexcept
      -> std::optional<static_error> {
    return check_text<errc::missing_required_attribute>(text, attribute, "type must be non-empty");
  }
};

// spec: SWR-CORE-0019
struct subject_policy {
  static constexpr std::string_view attribute = "subject";
  [[nodiscard]] static constexpr auto check(std::string_view text) noexcept
      -> std::optional<static_error> {
    return check_text<errc::invalid_attribute_value>(text, attribute,
                                                     "subject is present but empty");
  }
};

struct dataschema_policy {
  static constexpr std::string_view attribute = "dataschema";
  [[nodiscard]] static constexpr auto check(std::string_view text) noexcept
      -> std::optional<static_error> {
    return check_text<errc::invalid_attribute_value>(text, attribute,
                                                     "dataschema is present but empty");
  }
};

struct datacontenttype_policy {
  static constexpr std::string_view attribute = "datacontenttype";
  [[nodiscard]] static constexpr auto check(std::string_view text) noexcept
      -> std::optional<static_error> {
    if (const auto refused =
            check_text<errc::invalid_attribute_value>(text, attribute,
                                                      "datacontenttype is present but empty");
        refused) {
      return refused;
    }
    if (!ctre::match<content_type_pattern>(text)) {
      return static_error{
          .code = errc::invalid_content_type,
          .detail = "not a valid media type",
          .where = attribute,
      };
    }
    return {};
  }
};

// spec: SWR-CORE-0020
struct extension_name_policy {
  static constexpr std::string_view attribute = "extension";
  static constexpr bool names_offending_text = true;
  [[nodiscard]] static constexpr auto check(std::string_view text) noexcept
      -> std::optional<static_error> {
    if (!ce::v2::valid_attribute_name(text)) {
      return static_error{
          .code = errc::invalid_attribute_name,
          .detail = "extension names must match [a-z0-9]+",
          .where = attribute,
      };
    }
    if (ce::v2::reserved_name(text)) {
      return static_error{
          .code = errc::reserved_attribute_name,
          .detail = "an extension may not redefine a context attribute",
          .where = attribute,
      };
    }
    return {};
  }
};

}  // namespace detail

// spec: SWR-CORE-0026
using id = detail::validated_string<detail::id_policy>;
using source = detail::validated_string<detail::source_policy>;
using type = detail::validated_string<detail::type_policy>;
using subject = detail::validated_string<detail::subject_policy>;
using dataschema = detail::validated_string<detail::dataschema_policy>;
using datacontenttype = detail::validated_string<detail::datacontenttype_policy>;
using extension_name = detail::validated_string<detail::extension_name_policy>;

// spec: SWR-CORE-0018
class spec_version {
 public:
  constexpr spec_version() noexcept = default;

  [[nodiscard]] static auto make(std::string_view text) -> result<spec_version> {
    if (text != "1.0") {
      return fail(errc::unsupported_spec_version, "this SDK implements CloudEvents 1.0 only",
                  "specversion");
    }
    return spec_version{};
  }

  [[nodiscard]] static constexpr auto view() noexcept -> std::string_view { return "1.0"; }

  [[nodiscard]] friend auto operator==(spec_version, spec_version) noexcept -> bool = default;
};

// spec: SWR-CORE-0027
namespace literals {

consteval auto operator""_id(const char* text, std::size_t size) {
  return detail::literal<detail::id_policy>{text, size};
}
consteval auto operator""_source(const char* text, std::size_t size) {
  return detail::literal<detail::source_policy>{text, size};
}
consteval auto operator""_type(const char* text, std::size_t size) {
  return detail::literal<detail::type_policy>{text, size};
}
consteval auto operator""_subject(const char* text, std::size_t size) {
  return detail::literal<detail::subject_policy>{text, size};
}
consteval auto operator""_dataschema(const char* text, std::size_t size) {
  return detail::literal<detail::dataschema_policy>{text, size};
}
consteval auto operator""_mediatype(const char* text, std::size_t size) {
  return detail::literal<detail::datacontenttype_policy>{text, size};
}
consteval auto operator""_ext(const char* text, std::size_t size) {
  return detail::literal<detail::extension_name_policy>{text, size};
}

}  // namespace literals

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
using extension_value_t = extension_value_type<std::remove_cvref_t<F>>::type;

template <class F>
inline constexpr bool is_optional_field = false;
template <class U>
inline constexpr bool is_optional_field<std::optional<U>> = true;

// spec: SWR-EXT-0003
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
    return fail(errc::type_mismatch, R"(expected "true" or "false")", std::string{where});
  } else if constexpr (std::is_same_v<value_type, std::int32_t>) {
    std::int32_t parsed = 0;
    const char* const first = std::to_address(text.begin());
    const char* const last = std::to_address(text.end());
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

namespace detail {

template <class Attribute, class Text>
[[nodiscard]] auto store_attribute(std::optional<Attribute>& slot, Text&& text) -> result<void> {
  auto made = Attribute::make(std::forward<Text>(text));
  if (!made) {
    return fail(made.error().code, made.error().detail, made.error().where);
  }
  slot = std::move(*made);
  return {};
}

}  // namespace detail

}  // namespace ce::inline v2
