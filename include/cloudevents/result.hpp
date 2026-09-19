#pragma once

/// \file
/// \brief The error model: `errc`, `error`, `result<T>` and `fail`.
///
/// Every fallible operation in the SDK returns `ce::result<T>`; nothing throws.
/// See ADR-0001 for why, and ADR-0002 for the enforced subset of `result<T>` that
/// makes the C++20 polyfill removable without touching a call site.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/expected_polyfill.hpp>

#if CE_HAS_EXPECTED
#include <expected>
#endif

namespace ce::inline v1 {

/// \brief What went wrong, as a closed set.
///
/// There is deliberately no `ok` enumerator: an `error` only exists on the failure
/// path, and SWR-SEC-0003 requires a negative test per enumerator, which a success
/// value could not have.
enum class errc : std::uint8_t {
  /// A required context attribute is absent or empty (CloudEvents core §3).
  missing_required_attribute = 1,
  /// An extension attribute name is not `[a-z0-9]+` (core §4.1).
  invalid_attribute_name,
  /// An attribute name collides with a reserved context attribute (core §4.1).
  reserved_attribute_name,
  /// An attribute is present but its value is not valid for its type.
  invalid_attribute_value,
  /// `specversion` names a version this SDK does not implement (SPEC §9, D5).
  unsupported_spec_version,
  /// A timestamp is not a valid RFC 3339 date-time.
  invalid_timestamp,
  /// A content type is syntactically invalid (RFC 2046).
  invalid_content_type,
  /// The input is not well-formed for its format.
  parse_error,
  /// The value is well-formed but has the wrong CloudEvents type. Includes a
  /// floating-point extension value, which the type system has no room for
  /// (SPEC §9, D6).
  type_mismatch,
  /// An integer is outside the range the CloudEvents `Integer` type permits.
  out_of_range,
  /// Both `data` and `data_base64` are present (JSON format §3.1).
  data_conflict,
  /// Base64 input is not valid per RFC 4648 §4.
  invalid_base64,
  /// A byte sequence is not well-formed UTF-8.
  invalid_utf8,
  /// The message carries no `ce-specversion` and no CloudEvents content type, so
  /// it is not a CloudEvent at all. Kept distinct from a malformed one, because a
  /// receiver usually wants to pass these through rather than reject them
  /// (SWR-HTTP-0014).
  not_a_cloudevent,
  /// A described struct has a member whose type the SDK cannot map.
  unsupported_field_type,
};

/// \brief A human-readable name for an `errc`, for diagnostics and test output.
[[nodiscard]] constexpr auto to_string_view(errc code) noexcept -> std::string_view {
  switch (code) {
    case errc::missing_required_attribute: return "missing_required_attribute";
    case errc::invalid_attribute_name:     return "invalid_attribute_name";
    case errc::reserved_attribute_name:    return "reserved_attribute_name";
    case errc::invalid_attribute_value:    return "invalid_attribute_value";
    case errc::unsupported_spec_version:   return "unsupported_spec_version";
    case errc::invalid_timestamp:          return "invalid_timestamp";
    case errc::invalid_content_type:       return "invalid_content_type";
    case errc::parse_error:                return "parse_error";
    case errc::type_mismatch:              return "type_mismatch";
    case errc::out_of_range:               return "out_of_range";
    case errc::data_conflict:              return "data_conflict";
    case errc::invalid_base64:             return "invalid_base64";
    case errc::invalid_utf8:               return "invalid_utf8";
    case errc::not_a_cloudevent:           return "not_a_cloudevent";
    case errc::unsupported_field_type:     return "unsupported_field_type";
  }
  return "unknown";
}

/// \brief A failure: what went wrong, where, and in which terms.
///
/// An aggregate with the required member first and no default member initializer,
/// so a designated initializer that forgets `code` fails to compile on GCC.
/// `where` names the attribute or JSON pointer the failure concerns, so a decode
/// error is locatable in the offending document rather than merely named.
struct error {
  errc code;
  std::string detail = {};
  std::string where = {};
};

#if CE_HAS_EXPECTED

/// \brief The result of a fallible operation.
///
/// Aliases `std::expected` wherever the standard library provides it. Only the
/// subset listed in ADR-0002 may be used on this type inside the library; the
/// polyfill build is what enforces that.
template <class T>
using result = std::expected<T, error>;

using failure = std::unexpected<error>;

#else

template <class T>
using result = detail::poly::expected<T, error>;

using failure = detail::poly::unexpected<error>;

#endif

/// \brief Build a failure that converts into any `result<T>`.
///
/// Returns the unexpected carrier rather than a `result<T>`, so one helper serves
/// every return type on both backends with no deduction at the call site:
/// `return ce::fail(errc::parse_error, "trailing bytes");`
[[nodiscard]] inline auto fail(errc code, std::string detail = {}, std::string where = {})
    -> failure {
  return failure{error{.code = code, .detail = std::move(detail), .where = std::move(where)}};
}

}  // namespace ce::inline v1
