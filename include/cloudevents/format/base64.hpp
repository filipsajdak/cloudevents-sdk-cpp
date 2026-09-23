#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <cloudevents/core.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1 {

namespace detail {

inline constexpr std::string_view base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline constexpr std::uint8_t lowercase_offset = 26;
inline constexpr std::uint8_t digit_offset = 52;
inline constexpr std::uint8_t plus_value = 62;
inline constexpr std::uint8_t slash_value = 63;

inline constexpr std::uint8_t not_in_alphabet = 0xFF;

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

inline constexpr std::size_t quantum_characters = 4;
inline constexpr std::size_t quantum_octets = 3;

inline constexpr std::size_t max_padding_characters = 2;

inline constexpr unsigned sextet_bits = 6;
inline constexpr unsigned octet_bits = 8;
inline constexpr std::uint32_t sextet_mask = 0x3FU;
inline constexpr std::uint32_t octet_mask = 0xFFU;

inline constexpr unsigned first_sextet_shift = 18;
inline constexpr unsigned second_sextet_shift = 12;
inline constexpr unsigned third_sextet_shift = 6;
inline constexpr unsigned fourth_sextet_shift = 0;

inline constexpr unsigned one_octet_padding = 4;
inline constexpr unsigned two_octet_padding = 2;

[[nodiscard]] constexpr auto base64_character(std::uint32_t value) noexcept -> char {
  static_assert(base64_alphabet.size() == sextet_mask + 1);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  return base64_alphabet[value & sextet_mask];
}

}  // namespace detail

// spec: SWR-JSON-0027
[[nodiscard]] constexpr auto base64_encode(const binary& bytes) -> std::string {
  std::string out;
  out.reserve(((bytes.size() + detail::quantum_octets - 1) / detail::quantum_octets) *
              detail::quantum_characters);

  const auto emit = [&out](std::uint32_t packed, unsigned offset) {
    out.push_back(detail::base64_character(packed >> offset));
  };
  const auto pack = [](std::span<const std::byte> octets) {
    std::uint32_t group = 0;
    for (const std::byte octet : octets) {
      group = (group << detail::octet_bits) | static_cast<std::uint32_t>(octet);
    }
    return group;
  };

  std::span<const std::byte> rest{bytes};
  for (; rest.size() >= detail::quantum_octets; rest = rest.subspan(detail::quantum_octets)) {
    const std::uint32_t group = pack(rest.first(detail::quantum_octets));
    emit(group, detail::first_sextet_shift);
    emit(group, detail::second_sextet_shift);
    emit(group, detail::third_sextet_shift);
    emit(group, detail::fourth_sextet_shift);
  }

  if (rest.size() == 1) {
    const std::uint32_t group = pack(rest) << detail::one_octet_padding;
    emit(group, detail::third_sextet_shift);
    emit(group, detail::fourth_sextet_shift);
    out.append("==");
  } else if (rest.size() == 2) {
    const std::uint32_t group = pack(rest) << detail::two_octet_padding;
    emit(group, detail::second_sextet_shift);
    emit(group, detail::third_sextet_shift);
    emit(group, detail::fourth_sextet_shift);
    out.push_back('=');
  }

  return out;
}

// spec: SWR-JSON-0028
// spec: SWR-JSON-0029
// spec: SWR-JSON-0038
[[nodiscard]] constexpr auto base64_decode(std::string_view text) -> result<binary> {
  std::string_view body = text;
  std::size_t padding = 0;
  while (!body.empty() && body.back() == '=') {
    body.remove_suffix(1);
    ++padding;
  }

  if (padding > detail::max_padding_characters) {
    return fail(errc::invalid_base64, "more than two padding characters",
                std::string{text});
  }
  if (padding > 0 && text.size() % detail::quantum_characters != 0) {
    return fail(errc::invalid_base64, "padding does not complete the final quantum",
                std::string{text});
  }

  if (body.size() % detail::quantum_characters == 1) {
    return fail(errc::invalid_base64, "encoded length leaves a stray sextet", std::string{text});
  }

  binary out;
  out.reserve(((body.size() / detail::quantum_characters) * detail::quantum_octets) +
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

  if (bits > 0 && (accumulator & ((1U << bits) - 1)) != 0) {
    return fail(errc::invalid_base64, "unused trailing bits are not zero", std::string{text});
  }

  return out;
}

}  // namespace ce::inline v1
