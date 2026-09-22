#pragma once

/// \file
/// \brief The CloudEvents v1.0.2 event model, its attribute types and validation.
///
/// Strict on produce, tolerant on consume.

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

namespace ce::inline v1 {

/// \brief Raw payload bytes.
using binary = std::vector<std::byte>;

namespace detail {

/// \brief A string that carries which CloudEvents attribute type it is.
///
/// String, URI and URI-Reference share a wire form, so only the declared type
/// tells them apart. They must also be distinct types for `attribute_value` to
/// be a well-formed variant.
template <class Tag>
class tagged_string {
 public:
  tagged_string() = default;

  // Implicit on purpose: an exact `std::string` still selects the `std::string`
  // alternative, because an exact match beats a user-defined conversion.
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

/// \brief An absolute URI (CloudEvents `URI` type).
using uri = detail::tagged_string<detail::uri_tag>;

/// \brief A URI reference, absolute or relative (CloudEvents `URI-Reference`).
using uri_ref = detail::tagged_string<detail::uri_ref_tag>;

/// \brief The CloudEvents attribute type system. There is no floating-point
/// alternative; the CloudEvents type system has none.
using attribute_value =
    std::variant<bool, std::int32_t, std::string, binary, uri, uri_ref, timestamp>;

/// \brief Already-serialized JSON, carried opaquely. Core never parses it; the
/// format layer does (ADR-0004).
struct json_text {
  std::string raw;

  friend auto operator==(const json_text&, const json_text&) -> bool = default;
};

/// \brief An event payload: absent, text, bytes, or pre-serialized JSON.
using data_t = std::variant<std::monostate, std::string, binary, json_text>;

namespace detail {

/// An extension attribute name: lowercase alphanumerics only (core spec §4.1).
inline constexpr auto attribute_name_pattern = ctll::fixed_string{R"(^[a-z0-9]+$)"};

/// A media type with an optional parameter list, used to decide JSON-ness.
inline constexpr auto content_type_pattern =
    ctll::fixed_string{R"(^\s*([A-Za-z0-9!#$%&'*+.^_`|~\-]+)/([A-Za-z0-9!#$%&'*+.^_`|~\-]+)\s*(;.*)?$)"};

/// The reserved context attribute names. An extension may not shadow one.
inline constexpr std::array<std::string_view, 10> reserved_names = {
    "id",      "source",  "specversion", "type",    "datacontenttype",
    "dataschema", "subject", "time",     "data",    "data_base64",
};

/// \brief Lowercase an ASCII character, without `std::tolower`'s locale.
[[nodiscard]] constexpr auto ascii_lower(char character) noexcept -> char {
  return (character >= 'A' && character <= 'Z')
             ? static_cast<char>(character - 'A' + 'a')
             : character;
}

/// \brief Case-insensitive ASCII comparison, for media types and header names.
[[nodiscard]] constexpr auto iequals(std::string_view left, std::string_view right) noexcept
    -> bool {
  return std::ranges::equal(left, right, {}, ascii_lower, ascii_lower);
}

/// \brief True when a string ends with a suffix, compared case-insensitively.
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

/// The UTF-8 encoding, as the tables in Unicode 15 chapter 3 state it. Each
/// constant is named for what it tests rather than for its bit pattern, so a
/// transcription error reads as one.
namespace utf8 {

/// A byte below this is a one-byte sequence and its own code point.
inline constexpr unsigned char ascii_limit = 0x80U;

/// A continuation byte matches `10xxxxxx`.
///
/// The payload masks are `std::uint32_t`, not `unsigned char`, because they are
/// combined with the accumulating code point. Two `unsigned char` operands both
/// promote to `int`, which would make the assembly below signed arithmetic on
/// values that are not signed - the bare `0x3FU` this replaced was `unsigned
/// int` by suffix and did not.
inline constexpr unsigned char continuation_mask = 0xC0U;
inline constexpr unsigned char continuation_marker = 0x80U;
inline constexpr std::uint32_t continuation_payload = 0x3FU;
inline constexpr unsigned int continuation_bits = 6U;

/// A lead byte announces its length in its high bits, and carries the rest of
/// the code point in the low bits the matching payload mask keeps.
inline constexpr unsigned char two_byte_mask = 0xE0U;
inline constexpr unsigned char two_byte_marker = 0xC0U;
inline constexpr std::uint32_t two_byte_payload = 0x1FU;

inline constexpr unsigned char three_byte_mask = 0xF0U;
inline constexpr unsigned char three_byte_marker = 0xE0U;
inline constexpr std::uint32_t three_byte_payload = 0x0FU;

inline constexpr unsigned char four_byte_mask = 0xF8U;
inline constexpr unsigned char four_byte_marker = 0xF0U;
inline constexpr std::uint32_t four_byte_payload = 0x07U;

/// The smallest code point each length is allowed to encode. A sequence below
/// its own floor is an overlong encoding: a second spelling of a character that
/// a shorter sequence already spells.
inline constexpr std::uint32_t two_byte_floor = 0x80U;
inline constexpr std::uint32_t three_byte_floor = 0x800U;
inline constexpr std::uint32_t four_byte_floor = 0x10000U;

/// UTF-16 surrogates are not characters, and UTF-8 does not encode them.
inline constexpr std::uint32_t first_surrogate = 0xD800U;
inline constexpr std::uint32_t last_surrogate = 0xDFFFU;

/// The last code point Unicode defines.
inline constexpr std::uint32_t last_code_point = 0x10FFFFU;

}  // namespace utf8

/// \brief True when the bytes are well-formed UTF-8.
///
/// Rejects overlong encodings, surrogates and values above U+10FFFF, because each
/// of those is a way to smuggle a second spelling of the same text past a
/// consumer that compares strings.
/// What a lead byte announces: how many bytes the sequence has, and the bits of
/// the code point the lead itself carries. A length of zero is a byte that
/// cannot start a sequence.
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

/// The smallest code point a sequence of `length` bytes may encode. Below it
/// the sequence is overlong: a second spelling of what a shorter one spells.
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

/// True when a decoded multi-byte sequence names a code point UTF-8 may carry.
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

/// \brief True when `name` matches `[a-z0-9]+`. Length is a SHOULD, so it
/// surfaces through `lint()` rather than here.
[[nodiscard]] constexpr auto valid_attribute_name(std::string_view name) noexcept -> bool {
  return ctre::match<detail::attribute_name_pattern>(name);
}

/// \brief True when `name` is a reserved context attribute that an extension may
/// not redefine.
[[nodiscard]] constexpr auto reserved_name(std::string_view name) noexcept -> bool {
  return std::ranges::find(detail::reserved_names, name) != detail::reserved_names.end();
}

/// \brief True when a media type denotes JSON: any type whose subtype is `json`
/// or ends in `+json`, case-insensitively, with parameters allowed.
///
/// The subtype is spelled out rather than shown as a wildcard pattern, because
/// the pattern contains the two characters that end a C comment.
[[nodiscard]] constexpr auto is_json_content_type(std::string_view content_type) noexcept -> bool {
  const auto match = ctre::match<detail::content_type_pattern>(content_type);
  if (!match) {
    return false;
  }
  const auto subtype = match.get<2>().to_view();
  return detail::iequals(subtype, "json") || detail::iends_with(subtype, "+json");
}

namespace detail {

/// \brief The rules the context attributes are built from.
///
/// One `check` per attribute, `constexpr` so the same function serves the
/// compile-time literal path and the run-time factory. It returns a diagnosis
/// rather than a result: the compile-time path has no value to carry, because its
/// failure mode is a compile error (SWR-CORE-0026).

/// Non-empty and encodable. Nothing beyond that: `datacontenttype` and
/// `dataschema` have their own rules, and `source` is deliberately exempt from
/// RFC 3986 (SWR-CORE-0025).
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

struct extension_name_policy {
  static constexpr std::string_view attribute = "extension";
  static constexpr bool names_offending_text = true;
  [[nodiscard]] static constexpr auto check(std::string_view text) noexcept
      -> std::optional<static_error> {
    if (!ce::v1::valid_attribute_name(text)) {
      return static_error{
          .code = errc::invalid_attribute_name,
          .detail = "extension names must match [a-z0-9]+",
          .where = attribute,
      };
    }
    if (ce::v1::reserved_name(text)) {
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

/// \brief The context attributes, each unable to hold a value the specification
/// forbids (SWR-CORE-0026).
using id = detail::validated_string<detail::id_policy>;
using source = detail::validated_string<detail::source_policy>;
using type = detail::validated_string<detail::type_policy>;
using subject = detail::validated_string<detail::subject_policy>;
using dataschema = detail::validated_string<detail::dataschema_policy>;
using datacontenttype = detail::validated_string<detail::datacontenttype_policy>;
using extension_name = detail::validated_string<detail::extension_name_policy>;

/// \brief `specversion`, as the one value this SDK implements.
///
/// A type with a single inhabitant rather than a string: the produce side then
/// cannot express a version that does not exist, and the only rule left concerns
/// text arriving from a peer (SWR-CORE-0018).
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

/// \brief Attribute literals, checked when the translation unit is compiled.
///
/// A literal is the only ergonomic compile-time form: `f("abc")` where `f` takes
/// `ce::id` needs two user-defined conversions and does not compile, and no
/// arrangement removes that (SWR-CORE-0027).
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

/// \brief The field types an extension struct may declare.
///
/// Exactly the CloudEvents attribute type system, less Binary: recovering a
/// Binary attribute from its wire form needs base64, which belongs to the format
/// layer, and none of the documented extensions declares one.
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

/// \brief Recover a declared field type from the string a lossy wire form left.
///
/// The JSON format and the HTTP binary binding both carry an extension as text,
/// so this is where a declared Integer, Boolean, URI or Timestamp becomes one
/// again (SWR-EXT-0003).
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
    // The two spellings the JSON format produces for a Boolean attribute.
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

/// \brief Read one attribute into a declared field type.
/// \brief True when every described field of `Ext` maps to an attribute type.
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

  // The type survived, so no conversion is needed or wanted.
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

/// \brief The bytes of some text, as the core binary type.
///
/// Public because a binding's caller needs it: a payload arrives as bytes and is
/// often held as text. It lived in ce::http::detail until examples and suites
/// started reaching in for it, which made it public in all but name.
[[nodiscard]] inline auto to_bytes(std::string_view text) -> binary {
  binary out;
  out.reserve(text.size());
  for (const char character : text) {
    out.push_back(static_cast<std::byte>(character));
  }
  return out;
}

/// \brief The text of some bytes.
[[nodiscard]] inline auto to_text(const binary& bytes) -> std::string {
  std::string out;
  out.reserve(bytes.size());
  for (const std::byte value : bytes) {
    out.push_back(static_cast<char>(value));
  }
  return out;
}

/// \brief A SHOULD-level observation about an event: not an error.
struct lint_warning {
  std::string attribute;
  std::string message;

  friend auto operator==(const lint_warning&, const lint_warning&) -> bool = default;
};

/// \brief A CloudEvent.
///
/// Every attribute is a type that cannot hold a value the specification forbids,
/// so an invalid event has no representation and there is nothing left to check
/// after construction (SWR-CORE-0014).
class event {
 public:
  /// Keyed by the validated name, so no map entry can name an attribute the
  /// specification refuses. Transparent comparator, so a lookup still takes a
  /// `string_view` without building a key to look up with.
  using extension_map = std::map<extension_name, attribute_value, std::less<>>;

  /// \brief Everything the specification leaves optional.
  struct options {
    std::optional<ce::datacontenttype> datacontenttype = {};
    std::optional<ce::dataschema> dataschema = {};
    std::optional<ce::subject> subject = {};
    std::optional<timestamp> time = {};
    extension_map extensions = {};
    data_t data = {};

    friend auto operator==(const options&, const options&) -> bool = default;
  };

  explicit event(ce::id identifier, ce::source origin, ce::type kind, options rest)
      : id_{std::move(identifier)},
        source_{std::move(origin)},
        type_{std::move(kind)},
        rest_{std::move(rest)} {}

  explicit event(ce::id identifier, ce::source origin, ce::type kind)
      : event{std::move(identifier), std::move(origin), std::move(kind), options{}} {}

  /// \brief An event under construction, for a reader that learns the attributes
  /// one field at a time.
  ///
  /// A decoder cannot name all three required attributes in one expression
  /// because it does not have them until the message is exhausted. `build()` is
  /// where their absence is reported, and absence is its only failure: the
  /// attribute types refuse every other way of being wrong (SWR-CORE-0029).
  struct builder {
    std::optional<ce::id> id = {};
    std::optional<ce::source> source = {};
    std::optional<ce::type> type = {};
    options rest = {};

    [[nodiscard]] auto build() && -> result<event> {
      if (!id) {
        return fail(errc::missing_required_attribute, "id must be present", "id");
      }
      if (!source) {
        return fail(errc::missing_required_attribute, "source must be present", "source");
      }
      if (!type) {
        return fail(errc::missing_required_attribute, "type must be present", "type");
      }
      return event{std::move(*id), std::move(*source), std::move(*type), std::move(rest)};
    }
  };

  [[nodiscard]] auto id() const noexcept -> const ce::id& { return id_; }
  [[nodiscard]] auto source() const noexcept -> const ce::source& { return source_; }
  [[nodiscard]] auto type() const noexcept -> const ce::type& { return type_; }
  [[nodiscard]] static auto specversion() noexcept -> spec_version { return {}; }

  [[nodiscard]] auto datacontenttype() const noexcept -> const std::optional<ce::datacontenttype>& {
    return rest_.datacontenttype;
  }
  [[nodiscard]] auto dataschema() const noexcept -> const std::optional<ce::dataschema>& {
    return rest_.dataschema;
  }
  [[nodiscard]] auto subject() const noexcept -> const std::optional<ce::subject>& {
    return rest_.subject;
  }
  [[nodiscard]] auto time() const noexcept -> const std::optional<timestamp>& { return rest_.time; }
  [[nodiscard]] auto extensions() const noexcept -> const extension_map& { return rest_.extensions; }
  [[nodiscard]] auto data() const noexcept -> const data_t& { return rest_.data; }

  /// \brief Replace the payload, and say what it is.
  ///
  /// One call rather than two, because a payload and the media type describing it
  /// are one fact: setting them separately leaves a window where the event says
  /// its bytes are something they are not.
  void set_data(data_t payload, std::optional<ce::datacontenttype> media_type) {
    rest_.data = std::move(payload);
    rest_.datacontenttype = std::move(media_type);
  }

  friend auto operator==(const event&, const event&) -> bool = default;

  /// \brief Set an extension attribute.
  ///
  /// Infallible: `extension_name` cannot hold a name that is invalid or that
  /// redefines a context attribute, so the refusal happened where the name was
  /// made.
  void set_extension(extension_name name, attribute_value value) {
    rest_.extensions.insert_or_assign(std::move(name), std::move(value));
  }

  /// \brief Remove an extension attribute, reporting whether one was there.
  auto remove_extension(std::string_view name) -> bool {
    const auto found = rest_.extensions.find(name);
    if (found == rest_.extensions.end()) {
      return false;
    }
    rest_.extensions.erase(found);
    return true;
  }

  /// \brief Look up an extension attribute, or nullptr when absent.
  [[nodiscard]] auto extension(std::string_view name) const noexcept -> const attribute_value* {
    const auto found = rest_.extensions.find(name);
    return found == rest_.extensions.end() ? nullptr : &found->second;
  }

  /// \brief Read a described extension struct out of the extension attributes.
  ///
  /// An absent optional field yields `nullopt`; an absent required field is an
  /// error naming the attribute (SWR-EXT-0002).
  template <described Ext>
  [[nodiscard]] auto get() const -> result<Ext> {
    static_assert(detail::extension_fields_supported<Ext>(),
                  "an extension struct may only declare bool, int32_t, std::string, uri, "
                  "uri_ref or timestamp fields, optionally wrapped in std::optional");

    // Filled field by field because the fields are reached generically; a
    // designated initializer cannot name what only the describe seam knows.
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

  /// \brief Write a described extension struct into the extension attributes.
  ///
  /// Each field is stored under its declared type, which is what makes a value
  /// survive the next encode with its type intact. A `nullopt` optional removes
  /// the attribute, so the event matches the struct exactly afterwards.
  template <described Ext>
  [[nodiscard]] auto set(const Ext& value) -> result<void> {
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
          static_cast<void>(remove_extension(name));
          return;
        }
      }
      auto attribute = extension_name::make(name);
      if (!attribute) {
        mapping_error = fail(attribute.error().code, attribute.error().detail,
                             attribute.error().where);
        return;
      }
      if constexpr (detail::is_optional_field<field_type>) {
        set_extension(std::move(*attribute), attribute_value{*field});
      } else {
        set_extension(std::move(*attribute), attribute_value{field});
      }
    });
    return mapping_error;
  }

  /// \brief SHOULD-level observations. They never reject an event, because the
  /// specification permits every one of them.
  [[nodiscard]] auto lint() const -> std::vector<lint_warning> {
    std::vector<lint_warning> warnings;
    constexpr std::size_t recommended_name_length = 20;
    for (const auto& [name, value] : rest_.extensions) {
      if (name.size() > recommended_name_length) {
        warnings.push_back(lint_warning{
            .attribute = name.str(),
            .message = "extension names should be 20 characters or fewer",
        });
      }
    }
    return warnings;
  }

 private:
  ce::id id_;
  ce::source source_;
  ce::type type_;
  options rest_;
};

}  // namespace ce::inline v1
