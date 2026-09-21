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

/// Characters in one base64 quantum: four sextets carrying three octets.
inline constexpr std::size_t quantum_characters = 4;

/// RFC 4648 section 4 pads a final quantum out to four characters, so a
/// well-formed encoding carries at most two padding characters.
inline constexpr std::size_t max_padding_characters = 2;

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
/// Padding may be omitted entirely, because the encoded length already
/// determines the byte count. Where it is present it must be exactly what the
/// final quantum needs: at most two characters, completing the input to a
/// multiple of four. Anything outside the alphabet is rejected, including
/// whitespace and the URL-safe alphabet, which RFC 4648 section 4 does not
/// admit.
[[nodiscard]] constexpr auto base64_decode(std::string_view text) -> result<binary> {
  std::string_view body = text;
  std::size_t padding = 0;
  while (!body.empty() && body.back() == '=') {
    body.remove_suffix(1);
    ++padding;
  }

  // Padding is optional, but a spelling that carries it must carry exactly the
  // amount the final quantum needs. Accepting any run of '=' made "QQ======" and
  // "====" decode, and made several spellings of the same octets valid.
  if (padding > detail::max_padding_characters) {
    return fail(errc::invalid_base64, "more than two padding characters",
                std::string{text});
  }
  if (padding > 0 && text.size() % detail::quantum_characters != 0) {
    return fail(errc::invalid_base64, "padding does not complete the final quantum",
                std::string{text});
  }

  // Every character after the padding was stripped must be in the alphabet, and
  // a leftover of exactly one sextet cannot have come from any byte sequence.
  if (body.size() % detail::quantum_characters == 1) {
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
