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

/// \brief True when a byte must be percent-encoded in a header value.
///
/// The binding requires escaping anything outside printable ASCII, plus space,
/// double quote and percent. Space is excluded by the printable-range test, and
/// percent has to be escaped or decoding could not tell an escape from a literal.
[[nodiscard]] constexpr auto needs_escape(unsigned char byte) noexcept -> bool {
  return byte < 0x21 || byte > 0x7E || byte == '"' || byte == '%';
}

[[nodiscard]] constexpr auto hex_digit(unsigned value) noexcept -> char {
  return static_cast<char>(value < 10 ? '0' + value : 'A' + (value - 10));
}

[[nodiscard]] constexpr auto hex_value(char character) noexcept -> int {
  if (character >= '0' && character <= '9') {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + 10;
  }
  return -1;
}

/// \brief Percent-encode a header value, treating it as UTF-8 bytes.
[[nodiscard]] inline auto percent_encode(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    if (needs_escape(byte)) {
      out.push_back('%');
      out.push_back(hex_digit(byte >> 4U));
      out.push_back(hex_digit(byte & 0x0FU));
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
  std::string out;
  out.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '%') {
      out.push_back(text[index]);
      continue;
    }
    if (index + 2 >= text.size()) {
      return fail(errc::parse_error, "truncated percent escape", std::string{text});
    }
    const int high = hex_value(text[index + 1]);
    const int low = hex_value(text[index + 2]);
    if (high < 0 || low < 0) {
      return fail(errc::parse_error, "percent escape is not hexadecimal", std::string{text});
    }
    out.push_back(static_cast<char>((high << 4) | low));
    index += 2;
  }
  if (!ce::v1::detail::is_valid_utf8(out)) {
    return fail(errc::invalid_utf8, "the decoded header value is not well-formed UTF-8",
                std::string{text});
  }
  return out;
}

}  // namespace ce::inline v1::binding::detail
