#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/expected_polyfill.hpp>

#if CE_HAS_EXPECTED
#include <expected>
#endif

namespace ce::v1 {

// spec: SWR-CORE-0001
enum class errc : std::uint8_t {
  missing_required_attribute = 1,
  invalid_attribute_name,
  reserved_attribute_name,
  invalid_attribute_value,
  unsupported_spec_version,
  invalid_timestamp,
  invalid_content_type,
  parse_error,
  type_mismatch,
  out_of_range,
  data_conflict,
  invalid_base64,
  invalid_utf8,
  not_a_cloudevent,
  invalid_argument,
};

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

// spec: SWR-CORE-0028
struct static_error {
  errc code;
  std::string_view detail = {};  // NOLINT(scudoai-copy-view-member)
  std::string_view where = {};   // NOLINT(scudoai-copy-view-member)

  friend auto operator==(const static_error&, const static_error&) -> bool = default;
};

struct error {
  errc code;
  std::string detail = {};
  std::string where = {};
};

[[nodiscard]] inline auto widen(const static_error& diagnosis) -> error {
  return error{
      .code = diagnosis.code,
      .detail = std::string{diagnosis.detail},
      .where = std::string{diagnosis.where},
  };
}

// spec: SWR-CORE-0002
#if CE_HAS_EXPECTED

template <class T>
using result = std::expected<T, error>;

using failure = std::unexpected<error>;

#else

template <class T>
using result = detail::poly::expected<T, error>;

using failure = detail::poly::unexpected<error>;

#endif

// spec: SWR-CORE-0004
[[nodiscard]] inline auto fail(errc code, std::string detail = {}, std::string where = {})
    -> failure {
  return failure{error{.code = code, .detail = std::move(detail), .where = std::move(where)}};
}

[[nodiscard]] inline auto fail(const static_error& diagnosis) -> failure {
  return failure{widen(diagnosis)};
}

[[nodiscard]] inline auto fail(const static_error& diagnosis, std::string where) -> failure {
  return failure{error{
      .code = diagnosis.code,
      .detail = std::string{diagnosis.detail},
      .where = std::move(where),
  }};
}

}  // namespace ce::v1

namespace ce::v2 {
using ce::v1::errc;
using ce::v1::error;
using ce::v1::fail;
using ce::v1::failure;
using ce::v1::result;
using ce::v1::static_error;
using ce::v1::to_string_view;
using ce::v1::widen;
}  // namespace ce::v2

// spec: SWR-BUILD-0005
namespace ce::inline v3 {
using ce::v1::errc;
using ce::v1::error;
using ce::v1::fail;
using ce::v1::failure;
using ce::v1::result;
using ce::v1::static_error;
using ce::v1::to_string_view;
using ce::v1::widen;
}  // namespace ce::inline v3
