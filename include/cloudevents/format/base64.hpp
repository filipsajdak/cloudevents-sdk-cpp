#pragma once

/// \file
/// \brief RFC 4648 section 4 base64, usable in a constant expression.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <cloudevents/core.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1 {

namespace detail {

inline constexpr std::string_view base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// \brief Sextet for a base64 character, or 0xFF when it is not one.
[[nodiscard]] constexpr auto base64_value(char character) noexcept -> std::uint8_t {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<std::uint8_t>(character - 'A');
  }
  if (character >= 'a' && character <= 'z') {
    return static_cast<std::uint8_t>(character - 'a' + 26);
  }
  if (character >= '0' && character <= '9') {
    return static_cast<std::uint8_t>(character - '0' + 52);
  }
  if (character == '+') {
    return 62;
  }
  if (character == '/') {
    return 63;
  }
  return 0xFF;
}

}  // namespace detail

/// \brief Encode bytes as base64, with padding.
///
/// Not std::format: this packs bit fields and uses each as a table index, which is
/// not a formatting operation and has no format specifier.
[[nodiscard]] constexpr auto base64_encode(const binary& bytes) -> std::string {
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);

  // Emit the 6-bit field at `offset` of a packed group.
  const auto emit = [&out](std::uint32_t packed, unsigned offset) {
    out.push_back(detail::base64_alphabet[(packed >> offset) & 0x3F]);
  };
  const auto byte_at = [&bytes](std::size_t index) {
    return static_cast<std::uint32_t>(bytes[index]);
  };

  std::size_t index = 0;
  for (; index + 2 < bytes.size(); index += 3) {
    const std::uint32_t group =
        (byte_at(index) << 16) | (byte_at(index + 1) << 8) | byte_at(index + 2);
    emit(group, 18);
    emit(group, 12);
    emit(group, 6);
    emit(group, 0);
  }

  // A partial group is padded to a whole number of sextets, then to four
  // characters, so the encoded length always determines the byte count.
  if (const std::size_t remaining = bytes.size() - index; remaining == 1) {
    const std::uint32_t group = byte_at(index) << 4;
    emit(group, 6);
    emit(group, 0);
    out.append("==");
  } else if (remaining == 2) {
    const std::uint32_t group = (byte_at(index) << 10) | (byte_at(index + 1) << 2);
    emit(group, 12);
    emit(group, 6);
    emit(group, 0);
    out.push_back('=');
  }

  return out;
}

/// \brief Decode base64.
///
/// Padding is optional, because a peer may omit it and the encoded length still
/// determines the byte count. Anything outside the alphabet is rejected,
/// including whitespace and the URL-safe alphabet, which RFC 4648 section 4 does
/// not admit.
[[nodiscard]] constexpr auto base64_decode(std::string_view text) -> result<binary> {
  std::string_view body = text;
  while (!body.empty() && body.back() == '=') {
    body.remove_suffix(1);
  }

  // Every character after the padding was stripped must be in the alphabet, and
  // a leftover of exactly one sextet cannot have come from any byte sequence.
  if (body.size() % 4 == 1) {
    return fail(errc::invalid_base64, "encoded length leaves a stray sextet", std::string{text});
  }

  binary out;
  out.reserve((body.size() / 4) * 3 + 2);

  std::uint32_t accumulator = 0;
  int bits = 0;
  for (const char character : body) {
    const std::uint8_t sextet = detail::base64_value(character);
    if (sextet == 0xFF) {
      return fail(errc::invalid_base64, "character is not in the base64 alphabet",
                  std::string{text});
    }
    accumulator = (accumulator << 6) | sextet;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFF));
    }
  }

  // Bits left over must be zero: a decoder that ignored them would accept several
  // encodings of the same bytes, and two peers would disagree about equality.
  if (bits > 0 && (accumulator & ((1U << bits) - 1)) != 0) {
    return fail(errc::invalid_base64, "unused trailing bits are not zero", std::string{text});
  }

  return out;
}

}  // namespace ce::inline v1
