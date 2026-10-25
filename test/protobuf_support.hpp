#pragma once

/// \file
/// \brief Shared helpers of the Protobuf suites: wire messages assembled by hand from the field
/// numbers of cloudevents.proto at v1.0.2, so a decoder that agrees with the encoder but not with
/// the schema fails.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/core.hpp>
#include <cloudevents/detail/timestamp.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/result.hpp>

#include "equality.hpp"

namespace ce::test::pb {

namespace wire = ce::protobuf::detail;

// The field numbers of cloudevents.proto at v1.0.2 (SWR-PROTO-0003).
inline constexpr std::uint32_t id_field = 1;
inline constexpr std::uint32_t source_field = 2;
inline constexpr std::uint32_t spec_version_field = 3;
inline constexpr std::uint32_t type_field = 4;
inline constexpr std::uint32_t attributes_field = 5;
inline constexpr std::uint32_t binary_data_field = 6;
inline constexpr std::uint32_t text_data_field = 7;
inline constexpr std::uint32_t proto_data_field = 8;
inline constexpr std::uint32_t key_field = 1;
inline constexpr std::uint32_t value_field = 2;
inline constexpr std::uint32_t ce_boolean = 1;
inline constexpr std::uint32_t ce_integer = 2;
inline constexpr std::uint32_t ce_string = 3;
inline constexpr std::uint32_t ce_bytes = 4;
inline constexpr std::uint32_t ce_uri = 5;
inline constexpr std::uint32_t ce_uri_ref = 6;
inline constexpr std::uint32_t ce_timestamp = 7;
inline constexpr std::uint32_t seconds_field = 1;
inline constexpr std::uint32_t nanos_field = 2;
inline constexpr std::uint32_t type_url_field = 1;
inline constexpr std::uint32_t any_value_field = 2;
inline constexpr std::uint32_t unknown_field = 99;
inline constexpr std::uint32_t batch_events_field = 1;

/// The latest whole second a nanosecond timestamp holds, less one for the fraction.
inline constexpr std::int64_t last_seconds =
    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds::max()).count() - 1;

[[nodiscard]] inline auto concat(std::initializer_list<ce::binary> parts) -> ce::binary {
  ce::binary out;
  for (const auto& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

[[nodiscard]] inline auto text(std::uint32_t number, std::string_view body) -> ce::binary {
  ce::binary out;
  wire::put_field(out, number, body);
  return out;
}

[[nodiscard]] inline auto message(std::uint32_t number, const ce::binary& body) -> ce::binary {
  ce::binary out;
  wire::put_field(out, number, body);
  return out;
}

[[nodiscard]] inline auto varint(std::uint32_t field_number, std::uint64_t value) -> ce::binary {
  ce::binary out;
  wire::put_varint_field(out, field_number, value);
  return out;
}

[[nodiscard]] inline auto bytes_of(std::string_view raw) -> ce::binary {
  const auto octets = std::as_bytes(std::span{raw});
  return {octets.begin(), octets.end()};
}

/// An attribute map entry: the name and the encoded `CloudEventAttributeValue`.
[[nodiscard]] inline auto entry(std::string_view name, const ce::binary& value) -> ce::binary {
  return message(attributes_field, concat({text(key_field, name), message(value_field, value)}));
}

[[nodiscard]] inline auto string_entry(std::string_view name, std::string_view value)
    -> ce::binary {
  return entry(name, text(ce_string, value));
}

/// The four required attributes, then whatever the case adds.
[[nodiscard]] inline auto event_bytes(std::initializer_list<ce::binary> rest = {}) -> ce::binary {
  auto out = concat({text(id_field, "1"),
                     text(source_field, "/spec/test"),
                     text(spec_version_field, "1.0"),
                     text(type_field, "com.example.thing")});
  for (const auto& part : rest) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

[[nodiscard]] inline auto timestamp_body(std::uint64_t seconds, std::uint64_t nanos) -> ce::binary {
  return concat({varint(seconds_field, seconds), varint(nanos_field, nanos)});
}

[[nodiscard]] inline auto with_time(std::uint64_t seconds, std::uint64_t nanos) -> ce::binary {
  return event_bytes({entry("time", message(ce_timestamp, timestamp_body(seconds, nanos)))});
}

[[nodiscard]] inline auto any_body(std::string_view type_url, std::string_view value)
    -> ce::binary {
  return concat({text(type_url_field, type_url), text(any_value_field, value)});
}

[[nodiscard]] inline auto base_event(ce::event::options rest = {}) -> ce::event {
  using namespace ce::literals;
  return ce::event{"1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

[[nodiscard]] inline auto decoded(const ce::binary& encoded,
                                  ce::protobuf::decode_options options = {})
    -> ce::result<ce::event> {
  return ce::protobuf_format::decode(encoded, options);
}

/// True when decoding fails with exactly `code`.
[[nodiscard]] inline auto refused(const ce::binary& encoded,
                                  ce::errc code,
                                  ce::protobuf::decode_options options = {}) -> bool {
  const auto read = decoded(encoded, options);
  return !read.has_value() && read.error().code == code;
}

[[nodiscard]] inline auto encoded_form(const ce::event& cloud_event) -> ce::binary {
  auto written = ce::protobuf_format::encode(cloud_event);
  return written ? std::move(*written) : ce::binary{};
}

[[nodiscard]] inline auto utc_instant(std::string_view rfc3339) -> ce::timestamp {
  const auto parsed = ce::parse_timestamp(rfc3339);
  return parsed ? *parsed : ce::timestamp{};
}

/// Equality that ut's generic operators cannot hijack, so a failure needs no printable operands.
[[nodiscard]] inline auto same(const auto& left, const auto& right) -> bool {
  return ce_test::equal(left, right);
}

/// The same event after a trip through the wire.
[[nodiscard]] inline auto round_trip(const ce::event& cloud_event) -> std::optional<ce::event> {
  auto written = ce::protobuf_format::encode(cloud_event);
  if (!written) {
    return std::nullopt;
  }
  auto read = ce::protobuf_format::decode(*written);
  if (!read) {
    return std::nullopt;
  }
  return std::move(*read);
}

}  // namespace ce::test::pb
