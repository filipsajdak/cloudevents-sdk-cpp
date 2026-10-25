#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include <cloudevents/attributes.hpp>
#include <cloudevents/result.hpp>

// spec: SWR-PROTO-0013
namespace ce::inline v4::protobuf {

// spec: SWR-PROTO-0010
/// The wire format's own bound: a 64-bit value needs at most ten 7-bit groups.
inline constexpr std::size_t max_varint_bytes = 10;

namespace detail {

inline constexpr unsigned varint_payload_bits = 7;
inline constexpr std::uint8_t varint_payload_mask = 0x7F;
inline constexpr std::uint8_t varint_more_flag = 0x80;
/// The tenth byte holds bit 63 only, so anything above 1 does not fit 64 bits.
inline constexpr std::uint8_t last_varint_byte_limit = 1;

inline constexpr unsigned wire_type_bits = 3;
inline constexpr std::uint64_t wire_type_mask = 0x7;
/// Field numbers have 29 bits.
inline constexpr std::uint64_t max_field_number = 0x1FFF'FFFF;

inline constexpr std::size_t fixed32_size = 4;
inline constexpr std::size_t fixed64_size = 8;

/// The wire types a reader can skip. Groups (3, 4) and the reserved 6 and 7 are refused.
enum class wire_type : std::uint8_t {
  varint = 0,
  fixed64 = 1,
  length_delimited = 2,
  fixed32 = 5,
};

[[nodiscard]] constexpr auto skippable(std::uint64_t kind) noexcept -> bool {
  return kind == static_cast<std::uint64_t>(wire_type::varint) ||
         kind == static_cast<std::uint64_t>(wire_type::fixed64) ||
         kind == static_cast<std::uint64_t>(wire_type::length_delimited) ||
         kind == static_cast<std::uint64_t>(wire_type::fixed32);
}

struct field_header {
  std::uint32_t number;
  wire_type kind;
};

/// A cursor over untrusted bytes. Every length is compared with what remains before
/// it is used, so no offset arithmetic can overflow and nothing is allocated from a
/// length prefix. It views the caller's bytes, which must outlive the reader.
class reader {
 public:
  explicit reader(std::span<const std::byte> bytes) noexcept : rest_{bytes} {}

  [[nodiscard]] auto done() const noexcept -> bool { return rest_.empty(); }

  // spec: SWR-PROTO-0009
  [[nodiscard]] auto read_varint() -> result<std::uint64_t> {
    std::uint64_t value = 0;
    for (std::size_t index = 0; !rest_.empty(); ++index) {
      const auto byte = std::to_integer<std::uint8_t>(rest_.front());
      rest_ = rest_.subspan(1);
      if (index + 1 == max_varint_bytes && byte > last_varint_byte_limit) {
        return fail(errc::parse_error, "a varint does not fit 64 bits");
      }
      value |= std::uint64_t{static_cast<std::uint8_t>(byte & varint_payload_mask)}
               << (varint_payload_bits * index);
      if ((byte & varint_more_flag) == 0) {
        return value;
      }
    }
    return fail(errc::parse_error, "a varint is cut short");
  }

  [[nodiscard]] auto read_header() -> result<field_header> {
    auto tag = read_varint();
    if (!tag) {
      return ce::v4::detail::forward_failure(std::move(tag).error());
    }
    const std::uint64_t number = *tag >> wire_type_bits;
    const std::uint64_t kind = *tag & wire_type_mask;
    if (number == 0 || number > max_field_number) {
      return fail(errc::parse_error, "a field number is out of range");
    }
    if (!skippable(kind)) {
      return fail(errc::parse_error, "a wire type this reader cannot skip (a group)");
    }
    return field_header{
        .number = static_cast<std::uint32_t>(number),
        .kind = static_cast<wire_type>(kind),
    };
  }

  [[nodiscard]] auto read_length_delimited() -> result<std::span<const std::byte>> {
    auto size = read_varint();
    if (!size) {
      return ce::v4::detail::forward_failure(std::move(size).error());
    }
    if (*size > rest_.size()) {
      return fail(errc::parse_error, "a length runs past the end of the input");
    }
    const auto body = rest_.first(static_cast<std::size_t>(*size));
    rest_ = rest_.subspan(body.size());
    return body;
  }

  [[nodiscard]] auto skip(wire_type kind) -> result<void> {
    if (kind == wire_type::varint) {
      auto skipped = read_varint();
      return skipped ? result<void>{} : ce::v4::detail::forward_failure(std::move(skipped).error());
    }
    if (kind == wire_type::length_delimited) {
      auto skipped = read_length_delimited();
      return skipped ? result<void>{} : ce::v4::detail::forward_failure(std::move(skipped).error());
    }
    const std::size_t size = kind == wire_type::fixed64 ? fixed64_size : fixed32_size;
    if (size > rest_.size()) {
      return fail(errc::parse_error, "a fixed-width field is cut short");
    }
    rest_ = rest_.subspan(size);
    return {};
  }

 private:
  // NOLINTNEXTLINE(scudoai-copy-view-member,custom-scudoai-copy-view-member)
  std::span<const std::byte> rest_;
};

/// Views protobuf string bytes as text; the bytes outlive the view.
[[nodiscard]] inline auto text_view(std::span<const std::byte> bytes) noexcept -> std::string_view {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

/// A protobuf `string` must be well-formed UTF-8.
[[nodiscard]] inline auto read_text(std::span<const std::byte> bytes) -> result<std::string_view> {
  const auto text = text_view(bytes);
  if (!ce::v4::detail::is_valid_utf8(text)) {
    return fail(errc::invalid_utf8, "a string field is not well-formed UTF-8");
  }
  return text;
}

inline void put_varint(binary& out, std::uint64_t value) {
  while (value > varint_payload_mask) {
    out.push_back(static_cast<std::byte>((value & varint_payload_mask) | varint_more_flag));
    value >>= varint_payload_bits;
  }
  out.push_back(static_cast<std::byte>(value));
}

inline void put_tag(binary& out, std::uint32_t number, wire_type kind) {
  put_varint(out, (std::uint64_t{number} << wire_type_bits) | static_cast<std::uint64_t>(kind));
}

inline void put_varint_field(binary& out, std::uint32_t number, std::uint64_t value) {
  put_tag(out, number, wire_type::varint);
  put_varint(out, value);
}

inline void put_field(binary& out, std::uint32_t number, std::span<const std::byte> body) {
  put_tag(out, number, wire_type::length_delimited);
  put_varint(out, body.size());
  out.insert(out.end(), body.begin(), body.end());
}

inline void put_field(binary& out, std::uint32_t number, std::string_view body) {
  put_field(out, number, std::as_bytes(std::span{body}));
}

}  // namespace detail

}  // namespace ce::inline v4::protobuf
