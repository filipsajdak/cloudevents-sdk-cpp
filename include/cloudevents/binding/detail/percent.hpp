#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <cloudevents/core.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1::binding::detail {

inline constexpr unsigned char first_printable = 0x21;
inline constexpr unsigned char last_printable = 0x7E;

inline constexpr unsigned hex_shift = 4U;
inline constexpr unsigned hex_mask = 0x0FU;
inline constexpr unsigned hex_radix = 10U;

inline constexpr int not_a_hex_digit = -1;

inline constexpr std::size_t escape_length = 3;

[[nodiscard]] constexpr auto needs_escape(unsigned char byte) noexcept -> bool {
  return byte < first_printable || byte > last_printable || byte == '"' || byte == '%';
}

[[nodiscard]] constexpr auto hex_digit(unsigned value) noexcept -> char {
  return static_cast<char>(value < hex_radix ? '0' + value : 'A' + (value - hex_radix));
}

[[nodiscard]] constexpr auto hex_value(char character) noexcept -> int {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + static_cast<int>(hex_radix);
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + static_cast<int>(hex_radix);
  }
  return not_a_hex_digit;
}

[[nodiscard]] inline auto percent_encode(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    if (needs_escape(byte)) {
      out.push_back('%');
      out.push_back(hex_digit(byte >> hex_shift));
      out.push_back(hex_digit(byte & hex_mask));
    } else {
      out.push_back(character);
    }
  }
  return out;
}

[[nodiscard]] inline auto percent_decode(std::string_view text) -> result<std::string> {
  const std::string_view whole = text;
  std::string out;
  out.reserve(text.size());
  while (!text.empty()) {
    if (text.front() != '%') {
      out.push_back(text.front());
      text.remove_prefix(1);
      continue;
    }
    if (text.size() < escape_length) {
      return fail(errc::parse_error, "truncated percent escape", std::string{whole});
    }
    const std::string_view digits = text.substr(1, escape_length - 1);
    const int high = hex_value(digits.front());
    const int low = hex_value(digits.back());
    if (high < 0 || low < 0) {
      return fail(errc::parse_error, "percent escape is not hexadecimal", std::string{whole});
    }
    const auto byte =
        static_cast<unsigned>(high) << hex_shift | static_cast<unsigned>(low);
    out.push_back(static_cast<char>(static_cast<unsigned char>(byte)));
    text.remove_prefix(escape_length);
  }
  if (!ce::v1::detail::is_valid_utf8(out)) {
    return fail(errc::invalid_utf8, "the decoded header value is not well-formed UTF-8",
                std::string{whole});
  }
  return out;
}

}  // namespace ce::inline v1::binding::detail
