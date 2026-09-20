#pragma once

/// \file
/// \brief The error model: `errc`, `error`, `result<T>` and `fail`.
///
/// Nothing throws; every fallible operation returns `result<T>`. ADR-0001 has the
/// reasoning, ADR-0002 the permitted subset.

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

/// \brief What went wrong, as a closed set. There is no `ok`: an `error` exists
/// only on the failure path.
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
  /// Wrong CloudEvents type, including a floating-point extension value.
  type_mismatch,
  /// An integer is outside the range the CloudEvents `Integer` type permits.
  out_of_range,
  /// Both `data` and `data_base64` are present (JSON format §3.1).
  data_conflict,
  /// Base64 input is not valid per RFC 4648 §4.
  invalid_base64,
  /// A byte sequence is not well-formed UTF-8.
  invalid_utf8,
  /// Not a CloudEvent at all, kept distinct from a malformed one: a receiver
  /// usually passes these through rather than rejecting them.
  not_a_cloudevent,
  /// The call names an entry point that cannot serve this input, such as asking
  /// the single-event encoder for a batch.
  invalid_argument,
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
    case errc::invalid_argument:           return "invalid_argument";
  }
  return "unknown";
}

/// \brief A failure. `where` names the attribute or JSON pointer it concerns, so
/// a decode error is locatable in the offending document.
struct error {
  errc code;
  std::string detail = {};
  std::string where = {};
};

#if CE_HAS_EXPECTED

/// \brief The result of a fallible operation. Only the ADR-0002 subset may be
/// used on it inside the library.
template <class T>
using result = std::expected<T, error>;

using failure = std::unexpected<error>;

#else

template <class T>
using result = detail::poly::expected<T, error>;

using failure = detail::poly::unexpected<error>;

#endif

/// \brief Build a failure that converts into any `result<T>`.
[[nodiscard]] inline auto fail(errc code, std::string detail = {}, std::string where = {})
    -> failure {
  return failure{error{.code = code, .detail = std::move(detail), .where = std::move(where)}};
}

}  // namespace ce::inline v1
