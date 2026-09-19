#pragma once

/// \file
/// \brief The CloudEvents v1.0.2 event model, its attribute types and validation.
///
/// Strict on produce, tolerant on consume.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ctre.hpp>

#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/timestamp.hpp>
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
inline constexpr std::string_view reserved_names[] = {
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

/// \brief True when a string ends with a suffix, compared case-insensitively.
[[nodiscard]] constexpr auto iends_with(std::string_view text, std::string_view suffix) noexcept
    -> bool {
  return text.size() >= suffix.size() && iequals(text.substr(text.size() - suffix.size()), suffix);
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
  for (const auto reserved : detail::reserved_names) {
    if (reserved == name) {
      return true;
    }
  }
  return false;
}

/// \brief True when a media type denotes JSON: `*/json` or `*/*+json`,
/// case-insensitively, parameters allowed.
[[nodiscard]] constexpr auto is_json_content_type(std::string_view content_type) noexcept -> bool {
  const auto match = ctre::match<detail::content_type_pattern>(content_type);
  if (!match) {
    return false;
  }
  const auto subtype = match.get<2>().to_view();
  return detail::iequals(subtype, "json") || detail::iends_with(subtype, "+json");
}

/// \brief A SHOULD-level observation about an event: not an error.
struct lint_warning {
  std::string attribute;
  std::string message;

  friend auto operator==(const lint_warning&, const lint_warning&) -> bool = default;
};

/// \brief A CloudEvent. A public aggregate: `validate()` is the gate, not a
/// constructor.
struct event {
  std::string id;
  uri_ref source;
  std::string type;

  std::string specversion = "1.0";
  std::optional<std::string> datacontenttype = {};
  std::optional<uri> dataschema = {};
  std::optional<std::string> subject = {};
  std::optional<timestamp> time = {};

  /// Transparent comparator, so lookups take a `string_view` without allocating.
  std::map<std::string, attribute_value, std::less<>> extensions = {};

  data_t data = {};

  friend auto operator==(const event&, const event&) -> bool = default;

  /// \brief Set an extension attribute, rejecting an invalid or reserved name.
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

  /// \brief Look up an extension attribute, or nullptr when absent.
  [[nodiscard]] auto extension(std::string_view name) const noexcept -> const attribute_value* {
    const auto found = extensions.find(name);
    return found == extensions.end() ? nullptr : &found->second;
  }

  /// \brief Check the event against the MUST-level rules, reporting the first
  /// violation. `source` is checked for non-emptiness only.
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

  /// \brief SHOULD-level observations, kept out of `validate()` so they never
  /// reject an event the spec permits.
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

}  // namespace ce::inline v1
