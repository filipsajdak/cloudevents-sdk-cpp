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

/// Where each run of the alphabet begins, so the arithmetic below reads as a
/// position in the table rather than as a number.
inline constexpr std::uint8_t lowercase_offset = 26;
inline constexpr std::uint8_t digit_offset = 52;
inline constexpr std::uint8_t plus_value = 62;
inline constexpr std::uint8_t slash_value = 63;

/// Not a sextet. Every value the alphabet yields is below 64, so this cannot
/// collide with one.
inline constexpr std::uint8_t not_in_alphabet = 0xFF;

/// \brief Sextet for a base64 character, or `not_in_alphabet`.
[[nodiscard]] constexpr auto base64_value(char character) noexcept -> std::uint8_t {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<std::uint8_t>(character - 'A');
  }
  if (character >= 'a' && character <= 'z') {
    return static_cast<std::uint8_t>(character - 'a' + lowercase_offset);
  }
  if (character >= '0' && character <= '9') {
    return static_cast<std::uint8_t>(character - '0' + digit_offset);
  }
  if (character == '+') {
    return plus_value;
  }
  if (character == '/') {
    return slash_value;
  }
  return not_in_alphabet;
}

/// Characters in one base64 quantum: four sextets carrying three octets.
inline constexpr std::size_t quantum_characters = 4;
inline constexpr std::size_t quantum_octets = 3;

/// RFC 4648 section 4 pads a final quantum out to four characters, so a
/// well-formed encoding carries at most two padding characters.
inline constexpr std::size_t max_padding_characters = 2;

/// One base64 character carries six bits; one octet carries eight.
inline constexpr unsigned sextet_bits = 6;
inline constexpr unsigned octet_bits = 8;
inline constexpr std::uint32_t sextet_mask = 0x3FU;
inline constexpr std::uint32_t octet_mask = 0xFFU;

/// Where each sextet of a whole three-octet group sits once the group is packed
/// into one integer.
inline constexpr unsigned first_sextet_shift = 18;
inline constexpr unsigned second_sextet_shift = 12;
inline constexpr unsigned third_sextet_shift = 6;
inline constexpr unsigned fourth_sextet_shift = 0;

/// Where each octet of that group sits while it is being packed.
inline constexpr unsigned first_octet_shift = 16;
inline constexpr unsigned second_octet_shift = 8;

/// A partial group is left-padded up to a whole number of sextets: one octet
/// needs four bits of padding, two octets need two.
inline constexpr unsigned one_octet_padding = 4;
inline constexpr unsigned two_octet_padding = 2;
inline constexpr unsigned two_octet_first_shift = 10;

}  // namespace detail

/// \brief Encode bytes as base64, with padding.
///
/// Not std::format: this packs bit fields and uses each as a table index, which is
/// not a formatting operation and has no format specifier.
[[nodiscard]] constexpr auto base64_encode(const binary& bytes) -> std::string {
  std::string out;
  out.reserve(((bytes.size() + detail::quantum_octets - 1) / detail::quantum_octets) *
              detail::quantum_characters);

  // Emit the 6-bit field at `offset` of a packed group.
  const auto emit = [&out](std::uint32_t packed, unsigned offset) {
    out.push_back(detail::base64_alphabet[(packed >> offset) & detail::sextet_mask]);
  };
  const auto byte_at = [&bytes](std::size_t index) {
    return static_cast<std::uint32_t>(bytes[index]);
  };

  std::size_t index = 0;
  for (; index + detail::quantum_octets - 1 < bytes.size(); index += detail::quantum_octets) {
    const std::uint32_t group = (byte_at(index) << detail::first_octet_shift) |
                                (byte_at(index + 1) << detail::second_octet_shift) |
                                byte_at(index + 2);
    emit(group, detail::first_sextet_shift);
    emit(group, detail::second_sextet_shift);
    emit(group, detail::third_sextet_shift);
    emit(group, detail::fourth_sextet_shift);
  }

  // A partial group is padded to a whole number of sextets, then to four
  // characters, so the encoded length always determines the byte count.
  if (const std::size_t remaining = bytes.size() - index; remaining == 1) {
    const std::uint32_t group = byte_at(index) << detail::one_octet_padding;
    emit(group, detail::third_sextet_shift);
    emit(group, detail::fourth_sextet_shift);
    out.append("==");
  } else if (remaining == 2) {
    const std::uint32_t group = (byte_at(index) << detail::two_octet_first_shift) |
                                (byte_at(index + 1) << detail::two_octet_padding);
    emit(group, detail::second_sextet_shift);
    emit(group, detail::third_sextet_shift);
    emit(group, detail::fourth_sextet_shift);
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
  out.reserve((body.size() / detail::quantum_characters) * detail::quantum_octets +
              detail::max_padding_characters);

  std::uint32_t accumulator = 0;
  unsigned bits = 0;
  for (const char character : body) {
    const std::uint8_t sextet = detail::base64_value(character);
    if (sextet == detail::not_in_alphabet) {
      return fail(errc::invalid_base64, "character is not in the base64 alphabet",
                  std::string{text});
    }
    accumulator = (accumulator << detail::sextet_bits) | sextet;
    bits += detail::sextet_bits;
    if (bits >= detail::octet_bits) {
      bits -= detail::octet_bits;
      out.push_back(static_cast<std::byte>((accumulator >> bits) & detail::octet_mask));
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
