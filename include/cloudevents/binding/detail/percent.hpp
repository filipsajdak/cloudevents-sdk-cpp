#pragma once

/// \file
/// \brief Percent-encoding, for the bindings whose field values need it.
///
/// HTTP field values are a restricted ASCII grammar, so a header value has to be
/// escaped. Kafka record headers, AMQP application-properties and MQTT user
/// properties are UTF-8 strings or opaque bytes and do not. This header is
/// therefore included by the HTTP binding and not by the others, rather than
/// living somewhere every binding would drag it in from.

#include <cstddef>
#include <string>
#include <string_view>

#include <cloudevents/core.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1::binding::detail {

/// The printable ASCII range the binding specification names, exclusive of
/// space at its lower end: U+0021 through U+007E.
inline constexpr unsigned char first_printable = 0x21;
inline constexpr unsigned char last_printable = 0x7E;

/// A byte splits into two hex digits of four bits each.
inline constexpr unsigned hex_shift = 4U;
inline constexpr unsigned hex_mask = 0x0FU;
inline constexpr unsigned hex_radix = 10U;

/// Not a hex digit. Negative so it cannot be mistaken for a value.
inline constexpr int not_a_hex_digit = -1;

/// An escape is the percent sign and the two hex digits of one byte.
inline constexpr std::size_t escape_length = 3;

/// \brief True when a byte must be percent-encoded in a header value.
///
/// The binding requires escaping anything outside printable ASCII, plus space,
/// double quote and percent. Space is excluded by the printable-range test, and
/// percent has to be escaped or decoding could not tell an escape from a literal.
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

/// \brief Percent-encode a header value, treating it as UTF-8 bytes.
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

/// \brief Percent-decode a header value.
///
/// Any octet may be escaped, because a sender is free to escape more than the
/// minimum. The result must still be well-formed UTF-8.
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
    // Assembled unsigned. hex_value returns int because it reports "not a hex
    // digit" as -1, and shifting a signed value into the high bit of a byte is
    // implementation-defined once the result passes CHAR_MAX. Every compiler
    // this is built with does the obvious thing, which is why it has never
    // produced a wrong byte - but percent_decode reads from an untrusted peer,
    // and the rest of it checks lengths, escapes and encoding precisely because
    // of that.
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
