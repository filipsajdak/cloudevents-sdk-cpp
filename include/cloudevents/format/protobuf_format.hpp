#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <cloudevents/core.hpp>
#include <cloudevents/format/detail/protobuf_wire.hpp>
#include <cloudevents/result.hpp>

// spec: SWR-PROTO-0013
namespace ce::inline v4::protobuf {

// spec: SWR-PROTO-0002
inline constexpr std::string_view content_type = "application/cloudevents+protobuf";

namespace detail {
inline constexpr std::size_t bytes_per_kibibyte = 1024;
inline constexpr std::size_t bytes_per_mebibyte = bytes_per_kibibyte * bytes_per_kibibyte;
inline constexpr std::size_t default_encoded_mebibytes = 16;
}  // namespace detail

// spec: SWR-PROTO-0010
/// The largest encoded event the default options accept (owner ruling, CR-0009 item 5).
inline constexpr std::size_t default_max_encoded_size =
    detail::default_encoded_mebibytes * detail::bytes_per_mebibyte;
/// The most attribute map entries an event may carry by default.
inline constexpr std::size_t default_max_attribute_count = 1024;
/// The depth of the schema: event, map entry, value, Timestamp. `Any` is opaque and groups are
/// refused, so the decoder's call depth is this constant and no input can raise it.
inline constexpr std::size_t fixed_nesting_depth = 4;
/// The most events a batch may carry by default. Proposed by the owner's ruling (CR-0009
/// item 20) and adjustable in review; it was not measured against a workload.
inline constexpr std::size_t default_max_batch_event_count = 100'000;

// spec: SWR-PROTO-0024
inline constexpr std::string_view batch_content_type = "application/cloudevents-batch+protobuf";

// spec: SWR-PROTO-0001
// spec: SWR-PROTO-0010
struct decode_options {
  std::size_t max_encoded_size = default_max_encoded_size;
  std::size_t max_attribute_count = default_max_attribute_count;
  std::size_t max_batch_event_count = default_max_batch_event_count;
};

/// An event with the type URL of its `proto_data` value, which the event cannot hold apart
/// from its dataschema. The type URL is absent when the data was not `proto_data` or its URL was
/// empty.
// spec: SWR-PROTO-0026
struct detailed_event {
  ce::event event;
  std::optional<std::string> type_url = {};
};

/// An event with the value `Codec` parsed from its JSON payload; the event keeps the payload as
/// text or bytes and holds no document. The value is empty when the event is not JSON-typed or
/// has no payload.
// spec: SWR-PROTO-0025
template<json::json_codec Codec>
struct decoded_json {
  ce::event event;
  std::optional<typename Codec::value> payload = {};
};

namespace detail {

using bytes = std::span<const std::byte>;

// spec: SWR-PROTO-0003
namespace event_field {
inline constexpr std::uint32_t id = 1;
inline constexpr std::uint32_t source = 2;
inline constexpr std::uint32_t spec_version = 3;
inline constexpr std::uint32_t type = 4;
inline constexpr std::uint32_t attributes = 5;
inline constexpr std::uint32_t binary_data = 6;
inline constexpr std::uint32_t text_data = 7;
inline constexpr std::uint32_t proto_data = 8;
}  // namespace event_field

namespace entry_field {
inline constexpr std::uint32_t key = 1;
inline constexpr std::uint32_t value = 2;
}  // namespace entry_field

namespace value_arm {
inline constexpr std::uint32_t boolean = 1;
inline constexpr std::uint32_t integer = 2;
inline constexpr std::uint32_t string = 3;
inline constexpr std::uint32_t bytes = 4;
inline constexpr std::uint32_t uri = 5;
inline constexpr std::uint32_t uri_ref = 6;
inline constexpr std::uint32_t timestamp = 7;
}  // namespace value_arm

namespace timestamp_field {
inline constexpr std::uint32_t seconds = 1;
inline constexpr std::uint32_t nanos = 2;
}  // namespace timestamp_field

namespace any_field {
inline constexpr std::uint32_t type_url = 1;
inline constexpr std::uint32_t value = 2;
}  // namespace any_field

namespace batch_field {
inline constexpr std::uint32_t events = 1;
}  // namespace batch_field

inline constexpr std::string_view datacontenttype_name = "datacontenttype";
inline constexpr std::string_view dataschema_name = "dataschema";
inline constexpr std::string_view subject_name = "subject";
inline constexpr std::string_view time_name = "time";
inline constexpr std::string_view protobuf_media_type = "application/protobuf";

[[nodiscard]] inline auto propagate(error&& diagnosis) -> failure {
  return ce::v4::detail::forward_failure(std::move(diagnosis));
}

[[nodiscard]] inline auto propagate(error&& diagnosis, std::string_view where) -> failure {
  return ce::v4::detail::forward_failure(std::move(diagnosis), std::string{where});
}

/// One field read off the wire: a varint value or a length-delimited body. A fixed-width
/// field is skipped by the reader and never reaches a visitor. The body views the input of
/// the decode call, which outlives every `field`.
struct field {
  std::uint32_t number;
  wire_type kind;
  std::uint64_t varint = 0;
  bytes body = {};  // NOLINT(scudoai-copy-view-member,custom-scudoai-copy-view-member)
};

[[nodiscard]] inline auto read_field(reader& in) -> result<std::optional<field>> {
  auto head = in.read_header();
  if (!head) {
    return propagate(std::move(head).error());
  }
  field current{.number = head->number, .kind = head->kind};
  if (head->kind == wire_type::varint) {
    auto value = in.read_varint();
    if (!value) {
      return propagate(std::move(value).error());
    }
    current.varint = *value;
    return std::optional<field>{current};
  }
  if (head->kind == wire_type::length_delimited) {
    auto body = in.read_length_delimited();
    if (!body) {
      return propagate(std::move(body).error());
    }
    current.body = *body;
    return std::optional<field>{current};
  }
  if (auto skipped = in.skip(head->kind); !skipped) {
    return propagate(std::move(skipped).error());
  }
  return std::optional<field>{};
}

/// Calls `visit` for each varint and length-delimited field of a message body, in wire order.
template<class Visit>
[[nodiscard]] auto for_each_field(bytes body, const Visit& visit) -> result<void> {
  reader in{body};
  while (!in.done()) {
    auto current = read_field(in);
    if (!current) {
      return propagate(std::move(current).error());
    }
    if (const auto& read = *current; read) {
      if (auto visited = visit(*read); !visited) {
        return visited;
      }
    }
  }
  return {};
}

// spec: SWR-PROTO-0006
inline constexpr std::chrono::seconds max_timestamp_seconds =
    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds::max()) -
    std::chrono::seconds{1};
inline constexpr std::chrono::nanoseconds one_second{std::chrono::seconds{1}};
inline constexpr std::chrono::nanoseconds one_millisecond{std::chrono::milliseconds{1}};
inline constexpr std::chrono::nanoseconds one_microsecond{std::chrono::microseconds{1}};
inline constexpr fraction_digits millisecond_digits{3};
inline constexpr fraction_digits microsecond_digits{6};
inline constexpr fraction_digits nanosecond_digits{9};

/// The fewest of 0, 3, 6 and 9 digits that hold the nanoseconds.
[[nodiscard]] constexpr auto digits_holding(std::chrono::nanoseconds fraction) noexcept
    -> fraction_digits {
  if (fraction == std::chrono::nanoseconds::zero()) {
    return fraction_digits{};
  }
  if (fraction % one_millisecond == std::chrono::nanoseconds::zero()) {
    return millisecond_digits;
  }
  if (fraction % one_microsecond == std::chrono::nanoseconds::zero()) {
    return microsecond_digits;
  }
  return nanosecond_digits;
}

/// The UTC instant of `seconds` and `nanos`, or `invalid_timestamp` when the nanoseconds are not
/// a fraction of a second or the timestamp type cannot hold the second.
[[nodiscard]] inline auto make_instant(std::int64_t seconds, std::int64_t nanos)
    -> result<timestamp> {
  if (nanos < 0 || nanos >= one_second.count()) {
    return fail(errc::invalid_timestamp, "Timestamp.nanos is outside 0 to 999999999");
  }
  if (seconds > max_timestamp_seconds.count() || seconds < -max_timestamp_seconds.count()) {
    return fail(errc::invalid_timestamp,
                "the instant is outside the range of the timestamp type (years 1678 to 2262)");
  }
  const std::chrono::nanoseconds fraction{nanos};
  return timestamp{
      .utc =
          std::chrono::sys_time<std::chrono::nanoseconds>{std::chrono::seconds{seconds} + fraction},
      .fractional_digits = digits_holding(fraction),
  };
}

[[nodiscard]] inline auto read_timestamp(bytes body) -> result<timestamp> {
  std::int64_t seconds = 0;
  std::int64_t nanos = 0;
  auto walked = for_each_field(body, [&](const field& current) -> result<void> {
    if (current.kind == wire_type::varint && current.number == timestamp_field::seconds) {
      seconds = static_cast<std::int64_t>(current.varint);
    } else if (current.kind == wire_type::varint && current.number == timestamp_field::nanos) {
      nanos = static_cast<std::int64_t>(current.varint);
    }
    return {};
  });
  if (!walked) {
    return propagate(std::move(walked).error());
  }
  return make_instant(seconds, nanos);
}

[[nodiscard]] inline auto read_integer(std::uint64_t raw) -> result<attribute_value> {
  const auto value = static_cast<std::int64_t>(raw);
  if (value < std::numeric_limits<std::int32_t>::min() ||
      value > std::numeric_limits<std::int32_t>::max()) {
    return fail(errc::out_of_range, "ce_integer is outside the range of int32");
  }
  return attribute_value{static_cast<std::int32_t>(value)};
}

using arm = std::optional<attribute_value>;

[[nodiscard]] inline auto read_varint_arm(const field& current) -> result<arm> {
  if (current.number == value_arm::boolean) {
    return arm{attribute_value{current.varint != 0}};
  }
  if (current.number != value_arm::integer) {
    return arm{};
  }
  auto integer = read_integer(current.varint);
  if (!integer) {
    return propagate(std::move(integer).error());
  }
  return arm{std::move(*integer)};
}

[[nodiscard]] inline auto read_text_arm(const field& current) -> result<arm> {
  auto text = read_text(current.body);
  if (!text) {
    return propagate(std::move(text).error());
  }
  if (current.number == value_arm::uri) {
    return arm{attribute_value{uri{std::string{*text}}}};
  }
  if (current.number == value_arm::uri_ref) {
    return arm{attribute_value{uri_ref{std::string{*text}}}};
  }
  return arm{attribute_value{std::string{*text}}};
}

[[nodiscard]] inline auto read_length_arm(const field& current) -> result<arm> {
  if (current.number == value_arm::bytes) {
    return arm{attribute_value{binary{current.body.begin(), current.body.end()}}};
  }
  if (current.number == value_arm::timestamp) {
    auto instant = read_timestamp(current.body);
    if (!instant) {
      return propagate(std::move(instant).error());
    }
    return arm{attribute_value{*instant}};
  }
  if (current.number == value_arm::string || current.number == value_arm::uri ||
      current.number == value_arm::uri_ref) {
    return read_text_arm(current);
  }
  return arm{};
}

/// Reads a `CloudEventAttributeValue`; the last arm on the wire wins, and none gives nullopt.
[[nodiscard]] inline auto read_value(bytes body) -> result<arm> {
  arm value;
  auto walked = for_each_field(body, [&value](const field& current) -> result<void> {
    auto read =
        current.kind == wire_type::varint ? read_varint_arm(current) : read_length_arm(current);
    if (!read) {
      return propagate(std::move(read).error());
    }
    if (*read) {
      value = std::move(*read);
    }
    return {};
  });
  if (!walked) {
    return propagate(std::move(walked).error());
  }
  return value;
}

using attribute_map = std::map<std::string_view, arm, std::less<>>;

/// Reads one map entry into `attributes`; a repeated key is replaced, so the last one wins.
[[nodiscard]] inline auto read_entry(bytes body, attribute_map& attributes) -> result<void> {
  std::string_view key;
  arm value;
  auto walked = for_each_field(body, [&](const field& current) -> result<void> {
    if (current.kind != wire_type::length_delimited) {
      return {};
    }
    if (current.number == entry_field::key) {
      auto text = read_text(current.body);
      if (!text) {
        return propagate(std::move(text).error());
      }
      key = *text;
    } else if (current.number == entry_field::value) {
      auto read = read_value(current.body);
      if (!read) {
        return propagate(std::move(read).error());
      }
      value = std::move(*read);
    }
    return {};
  });
  if (!walked) {
    return walked;
  }
  attributes.insert_or_assign(key, std::move(value));
  return {};
}

enum class data_field : std::uint8_t { none, binary_data, text_data, proto_data };

/// The wire message with nothing checked beyond its structure. The views point into the input
/// of the decode call, which outlives the `raw_event`.
struct raw_event {
  std::string_view id;       // NOLINT(scudoai-copy-view-member,custom-scudoai-copy-view-member)
  std::string_view source;   // NOLINT(scudoai-copy-view-member,custom-scudoai-copy-view-member)
  std::string_view version;  // NOLINT(scudoai-copy-view-member,custom-scudoai-copy-view-member)
  std::string_view type;     // NOLINT(scudoai-copy-view-member,custom-scudoai-copy-view-member)
  attribute_map attributes;
  std::size_t attribute_entries = 0;
  data_field data_kind = data_field::none;
  bytes data = {};            // NOLINT(scudoai-copy-view-member,custom-scudoai-copy-view-member)
  std::string_view type_url;  // NOLINT(scudoai-copy-view-member,custom-scudoai-copy-view-member)
};

inline constexpr std::size_t percent_escape_digits = 2;
inline constexpr std::string_view uri_punctuation = "-._~:/?#[]@!$&'()*+,;=";

[[nodiscard]] constexpr auto is_ascii_alphanumeric(char character) noexcept -> bool {
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9');
}

[[nodiscard]] constexpr auto is_hex_digit(char character) noexcept -> bool {
  return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
         (character >= 'A' && character <= 'F');
}

// spec: SWR-PROTO-0026
/// A character-level check of RFC 3986: every character is allowed in a URI-reference, a
/// percent sign starts a two-digit escape, and there is at most one fragment delimiter. The
/// grammar of the parts is not parsed (D-PROTO-3).
[[nodiscard]] constexpr auto valid_uri_reference(std::string_view text) noexcept -> bool {
  std::size_t escape_digits_due = 0;
  for (const char character : text) {
    if (escape_digits_due > 0) {
      if (!is_hex_digit(character)) {
        return false;
      }
      --escape_digits_due;
    } else if (character == '%') {
      escape_digits_due = percent_escape_digits;
    } else if (!is_ascii_alphanumeric(character) &&
               uri_punctuation.find(character) == std::string_view::npos) {
      return false;
    }
  }
  return escape_digits_due == 0 && std::ranges::count(text, '#') <= 1;
}

[[nodiscard]] inline auto store_required(const field& current, raw_event& into) -> result<void> {
  auto text = read_text(current.body);
  if (!text) {
    return propagate(std::move(text).error());
  }
  switch (current.number) {
    case event_field::id:
      into.id = *text;
      break;
    case event_field::source:
      into.source = *text;
      break;
    case event_field::spec_version:
      into.version = *text;
      break;
    default:
      into.type = *text;
      break;
  }
  return {};
}

[[nodiscard]] inline auto store_attribute_entry(const field& current,
                                                const decode_options& options,
                                                raw_event& into) -> result<void> {
  if (++into.attribute_entries > options.max_attribute_count) {
    return fail(errc::out_of_range, "the event has more attributes than the limit allows");
  }
  return read_entry(current.body, into.attributes);
}

// spec: SWR-PROTO-0008
/// Reads `google.protobuf.Any`: the type URL, checked as a URI-reference, and the value bytes.
[[nodiscard]] inline auto store_any(bytes body, raw_event& into) -> result<void> {
  std::string_view type_url;
  bytes value;
  auto walked = for_each_field(body, [&](const field& current) -> result<void> {
    if (current.kind != wire_type::length_delimited) {
      return {};
    }
    if (current.number == any_field::value) {
      value = current.body;
    } else if (current.number == any_field::type_url) {
      auto text = read_text(current.body);
      if (!text) {
        return propagate(std::move(text).error());
      }
      type_url = *text;
    }
    return {};
  });
  if (!walked) {
    return walked;
  }
  if (!valid_uri_reference(type_url)) {
    return fail(errc::parse_error, "the Any type URL is not a URI-reference", "proto_data");
  }
  into.type_url = type_url;
  into.data = value;
  return {};
}

[[nodiscard]] inline auto store_data(const field& current, raw_event& into) -> result<void> {
  into.type_url = {};
  into.data = current.body;
  if (current.number == event_field::binary_data) {
    into.data_kind = data_field::binary_data;
    return {};
  }
  if (current.number == event_field::proto_data) {
    into.data_kind = data_field::proto_data;
    return store_any(current.body, into);
  }
  into.data_kind = data_field::text_data;
  if (auto text = read_text(current.body); !text) {
    return propagate(std::move(text).error());
  }
  return {};
}

[[nodiscard]] inline auto store_field(const field& current,
                                      const decode_options& options,
                                      raw_event& into) -> result<void> {
  if (current.kind != wire_type::length_delimited) {
    return {};
  }
  // The reader refuses field number 0, so the fields 1 to 4 are the numbers up to 4.
  if (current.number <= event_field::type) {
    return store_required(current, into);
  }
  if (current.number == event_field::attributes) {
    return store_attribute_entry(current, options, into);
  }
  // The numbers 1 to 5 were handled above, so what is left in range is the three data fields.
  if (current.number <= event_field::proto_data) {
    return store_data(current, into);
  }
  return {};
}

[[nodiscard]] inline auto scan_event(bytes body, const decode_options& options)
    -> result<raw_event> {
  raw_event found;
  auto walked = for_each_field(body, [&](const field& current) -> result<void> {
    return store_field(current, options, found);
  });
  if (!walked) {
    return propagate(std::move(walked).error());
  }
  return found;
}

[[nodiscard]] inline auto text_of_string_arm(const attribute_value& value)
    -> std::optional<std::string_view> {
  if (const auto* text = std::get_if<std::string>(&value)) {
    return std::string_view{*text};
  }
  return std::nullopt;
}

[[nodiscard]] inline auto text_of_uri_arm(const attribute_value& value)
    -> std::optional<std::string_view> {
  if (const auto* absolute = std::get_if<uri>(&value)) {
    return absolute->view();
  }
  if (const auto* reference = std::get_if<uri_ref>(&value)) {
    return reference->view();
  }
  return std::nullopt;
}

template<class Attribute>
[[nodiscard]] auto store_text(std::optional<Attribute>& slot,
                              std::optional<std::string_view> text,
                              std::string_view name) -> result<void> {
  if (!text) {
    return fail(errc::type_mismatch,
                "the attribute is carried in the wrong arm of its value",
                std::string{name});
  }
  return ce::v4::detail::store_attribute(slot, *text);
}

[[nodiscard]] inline auto store_time(const attribute_value& value, std::optional<timestamp>& slot)
    -> result<void> {
  const auto* instant = std::get_if<timestamp>(&value);
  if (instant == nullptr) {
    return fail(errc::type_mismatch,
                "time is carried in the wrong arm of its value",
                std::string{time_name});
  }
  slot = *instant;
  return {};
}

[[nodiscard]] inline auto store_extension(std::string_view name,
                                          attribute_value&& value,
                                          event::options& into) -> result<void> {
  auto attribute = extension_name::make(name);
  if (!attribute) {
    return propagate(std::move(attribute).error(), name);
  }
  into.extensions.insert_or_assign(std::move(*attribute), std::move(value));
  return {};
}

// spec: SWR-PROTO-0004
// spec: SWR-PROTO-0005
[[nodiscard]] inline auto apply_attribute(std::string_view name,
                                          attribute_value&& value,
                                          event::options& into) -> result<void> {
  if (name == datacontenttype_name) {
    return store_text(into.datacontenttype, text_of_string_arm(value), name);
  }
  if (name == dataschema_name) {
    return store_text(into.dataschema, text_of_uri_arm(value), name);
  }
  if (name == subject_name) {
    return store_text(into.subject, text_of_string_arm(value), name);
  }
  if (name == time_name) {
    return store_time(value, into.time);
  }
  return store_extension(name, std::move(value), into);
}

[[nodiscard]] inline auto apply_attributes(attribute_map& attributes, event::options& into)
    -> result<void> {
  for (auto& [name, value] : attributes) {
    if (!value) {
      return fail(
          errc::invalid_attribute_value, "an attribute value has no arm set", std::string{name});
    }
    if (auto applied = apply_attribute(name, std::move(*value), into); !applied) {
      return applied;
    }
  }
  return {};
}

// spec: SWR-PROTO-0008
/// A `proto_data` value is a binary payload; the type URL fills a missing dataschema.
[[nodiscard]] inline auto apply_proto_data(const raw_event& raw, event::options& into)
    -> result<void> {
  if (!into.datacontenttype) {
    using namespace ce::literals;
    into.datacontenttype = "application/protobuf"_mediatype;
  }
  if (!into.dataschema && !raw.type_url.empty()) {
    auto schema = dataschema::make(raw.type_url);
    if (!schema) {
      return propagate(std::move(schema).error());
    }
    into.dataschema = std::move(*schema);
  }
  into.data = binary{raw.data.begin(), raw.data.end()};
  return {};
}

// spec: SWR-PROTO-0008
[[nodiscard]] inline auto apply_data(const raw_event& raw, event::options& into) -> result<void> {
  switch (raw.data_kind) {
    case data_field::none:
      return {};
    case data_field::text_data:
      into.data = std::string{text_view(raw.data)};
      return {};
    case data_field::binary_data:
      if (!raw.data.empty() || into.datacontenttype) {
        into.data = binary{raw.data.begin(), raw.data.end()};
      }
      return {};
    case data_field::proto_data:
      break;
  }
  return apply_proto_data(raw, into);
}

[[nodiscard]] inline auto build_event(raw_event& raw) -> result<event> {
  // spec: SWR-PROTO-0009
  if (auto version = spec_version::make(raw.version); !version) {
    return propagate(std::move(version).error());
  }
  event::builder builder{};
  if (auto stored = ce::v4::detail::store_attribute(builder.id, raw.id); !stored) {
    return propagate(std::move(stored).error());
  }
  if (auto stored = ce::v4::detail::store_attribute(builder.source, raw.source); !stored) {
    return propagate(std::move(stored).error());
  }
  if (auto stored = ce::v4::detail::store_attribute(builder.type, raw.type); !stored) {
    return propagate(std::move(stored).error());
  }
  if (auto applied = apply_attributes(raw.attributes, builder.rest); !applied) {
    return propagate(std::move(applied).error());
  }
  if (auto applied = apply_data(raw, builder.rest); !applied) {
    return propagate(std::move(applied).error());
  }
  return std::move(builder).build();
}

/// Reads the structure of one encoded event; the size limit is checked before any of it is read.
[[nodiscard]] inline auto scan_limited(bytes encoded, const decode_options& options)
    -> result<raw_event> {
  if (encoded.size() > options.max_encoded_size) {
    return fail(errc::out_of_range, "the encoded event is larger than the limit allows");
  }
  return scan_event(encoded, options);
}

[[nodiscard]] inline auto decode_event(bytes encoded, const decode_options& options)
    -> result<event> {
  auto raw = scan_limited(encoded, options);
  if (!raw) {
    return propagate(std::move(raw).error());
  }
  return build_event(*raw);
}

// spec: SWR-PROTO-0026
[[nodiscard]] inline auto decode_detailed_event(bytes encoded, const decode_options& options)
    -> result<detailed_event> {
  auto raw = scan_limited(encoded, options);
  if (!raw) {
    return propagate(std::move(raw).error());
  }
  std::optional<std::string> type_url;
  if (raw->data_kind == data_field::proto_data && !raw->type_url.empty()) {
    type_url = std::string{raw->type_url};
  }
  auto built = build_event(*raw);
  if (!built) {
    return propagate(std::move(built).error());
  }
  return detailed_event{.event = std::move(*built), .type_url = std::move(type_url)};
}

// spec: SWR-PROTO-0024
/// The bodies of the events of a `CloudEventBatch`. The count is checked before a body is
/// recorded, so a batch above the limit allocates for no more than the limit.
[[nodiscard]] inline auto batch_bodies(bytes encoded, const decode_options& options)
    -> result<std::vector<bytes>> {
  std::vector<bytes> bodies;
  auto walked = for_each_field(encoded, [&](const field& current) -> result<void> {
    if (current.kind != wire_type::length_delimited || current.number != batch_field::events) {
      return {};
    }
    if (bodies.size() >= options.max_batch_event_count) {
      return fail(errc::out_of_range, "the batch has more events than the limit allows");
    }
    bodies.push_back(current.body);
    return {};
  });
  if (!walked) {
    return propagate(std::move(walked).error());
  }
  return bodies;
}

// spec: SWR-PROTO-0024
[[nodiscard]] inline auto decode_events(bytes encoded, const decode_options& options)
    -> result<std::vector<event>> {
  auto bodies = batch_bodies(encoded, options);
  if (!bodies) {
    return propagate(std::move(bodies).error());
  }
  std::vector<event> events;
  for (const auto body : *bodies) {
    auto decoded = decode_event(body, options);
    if (!decoded) {
      return propagate(std::move(decoded).error());
    }
    events.push_back(std::move(*decoded));
  }
  return events;
}

// spec: SWR-PROTO-0025
template<json::json_codec Codec>
[[nodiscard]] auto parse_json_payload(const event& cloud_event)
    -> result<std::optional<typename Codec::value>> {
  const auto& media_type = cloud_event.datacontenttype();
  if (!media_type || !is_json_content_type(media_type->view())) {
    return std::optional<typename Codec::value>{};
  }
  std::optional<std::string_view> text;
  if (const auto* held = std::get_if<std::string>(&cloud_event.data())) {
    text = *held;
  } else if (const auto* octets = std::get_if<binary>(&cloud_event.data())) {
    text = text_view(*octets);
  }
  if (!text) {
    return std::optional<typename Codec::value>{};
  }
  auto parsed = Codec::parse(*text);
  if (!parsed) {
    return propagate(std::move(parsed).error(), "data");
  }
  return std::optional<typename Codec::value>{std::move(*parsed)};
}

inline void put_arm(binary& out, bool value) {
  put_varint_field(out, value_arm::boolean, value ? 1U : 0U);
}

inline void put_arm(binary& out, std::int32_t value) {
  put_varint_field(
      out, value_arm::integer, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

// A string_view would make a std::string ambiguous with the implicit conversion to uri.
// NOLINTNEXTLINE(custom-scudoai-copy-string-ref-param)
inline void put_arm(binary& out, const std::string& value) {
  put_field(out, value_arm::string, value);
}

inline void put_arm(binary& out, const binary& value) {
  put_field(out, value_arm::bytes, value);
}

inline void put_arm(binary& out, const uri& value) {
  put_field(out, value_arm::uri, value.view());
}

inline void put_arm(binary& out, const uri_ref& value) {
  put_field(out, value_arm::uri_ref, value.view());
}

// spec: SWR-PROTO-0006
inline void put_arm(binary& out, const timestamp& value) {
  const auto since_epoch = value.utc.time_since_epoch();
  const auto seconds = std::chrono::floor<std::chrono::seconds>(since_epoch);
  const auto nanos = since_epoch - seconds;
  binary body;
  if (seconds != std::chrono::seconds::zero()) {
    put_varint_field(body, timestamp_field::seconds, static_cast<std::uint64_t>(seconds.count()));
  }
  if (nanos != std::chrono::nanoseconds::zero()) {
    put_varint_field(body, timestamp_field::nanos, static_cast<std::uint64_t>(nanos.count()));
  }
  put_field(out, value_arm::timestamp, body);
}

[[nodiscard]] inline auto encode_value(const attribute_value& value) -> binary {
  binary out;
  std::visit([&out](const auto& held) { put_arm(out, held); }, value);
  return out;
}

using attribute_bodies = std::map<std::string_view, binary, std::less<>>;

// spec: SWR-PROTO-0011
/// The attributes map in ascending order of name, context attributes and extensions alike.
[[nodiscard]] inline auto encode_attributes(const event& cloud_event) -> attribute_bodies {
  attribute_bodies bodies;
  if (const auto& media_type = cloud_event.datacontenttype(); media_type) {
    bodies.emplace(datacontenttype_name, encode_value(std::string{media_type->view()}));
  }
  if (const auto& schema = cloud_event.dataschema(); schema) {
    bodies.emplace(dataschema_name, encode_value(uri{schema->str()}));
  }
  if (const auto& subject = cloud_event.subject(); subject) {
    bodies.emplace(subject_name, encode_value(std::string{subject->view()}));
  }
  if (const auto& instant = cloud_event.time(); instant) {
    bodies.emplace(time_name, encode_value(*instant));
  }
  for (const auto& [name, value] : cloud_event.extensions()) {
    bodies.emplace(name.view(), encode_value(value));
  }
  return bodies;
}

inline void put_entry(binary& out, std::string_view name, const binary& value) {
  binary entry;
  put_field(entry, entry_field::key, name);
  put_field(entry, entry_field::value, value);
  put_field(out, event_field::attributes, entry);
}

/// The dataschema as the `Any` type URL, when the event is a protobuf payload that has one.
[[nodiscard]] inline auto proto_type_url(const event& cloud_event)
    -> std::optional<std::string_view> {
  const auto& media_type = cloud_event.datacontenttype();
  const auto& schema = cloud_event.dataschema();
  if (media_type && schema && ce::v4::detail::iequals(media_type->view(), protobuf_media_type) &&
      valid_uri_reference(schema->view())) {
    return schema->view();
  }
  return std::nullopt;
}

inline void put_any(binary& out, std::string_view type_url, bytes value) {
  binary any;
  if (!type_url.empty()) {
    put_field(any, any_field::type_url, type_url);
  }
  put_field(any, any_field::value, value);
  put_field(out, event_field::proto_data, any);
}

/// The `Any` type URL to write for the data, if the data goes to `proto_data`.
using any_url = std::optional<std::string_view>;

// spec: SWR-PROTO-0007
inline void put_data(binary& out, const any_url& type_url, const binary& value) {
  if (type_url) {
    put_any(out, *type_url, value);
  } else {
    put_field(out, event_field::binary_data, value);
  }
}

inline void put_data(binary& out, const any_url& /*type_url*/, std::string_view value) {
  put_field(out, event_field::text_data, value);
}

inline void put_data(binary& out, const any_url& /*type_url*/, const json_text& value) {
  put_field(out, event_field::text_data, value.raw);
}

inline void put_data(binary& out, const any_url& /*type_url*/, const json_document& value) {
  put_field(out, event_field::text_data, value.dump());
}

inline void put_data(binary& /*out*/,
                     const any_url& /*type_url*/,
                     const std::monostate& /*value*/) {}

// spec: SWR-PROTO-0012
[[nodiscard]] inline auto encode_event(const event& cloud_event, const any_url& type_url)
    -> binary {
  binary out;
  put_field(out, event_field::id, cloud_event.id().view());
  put_field(out, event_field::source, cloud_event.source().view());
  put_field(out, event_field::spec_version, spec_version::view());
  put_field(out, event_field::type, cloud_event.type().view());
  for (const auto& [name, body] : encode_attributes(cloud_event)) {
    put_entry(out, name, body);
  }
  std::visit([&](const auto& value) { put_data(out, type_url, value); }, cloud_event.data());
  return out;
}

// spec: SWR-PROTO-0026
[[nodiscard]] inline auto encode_event_as_any(const event& cloud_event, std::string_view type_url)
    -> result<binary> {
  if (!valid_uri_reference(type_url)) {
    return fail(errc::parse_error, "the type URL is not a URI-reference", "proto_data");
  }
  if (!std::holds_alternative<binary>(cloud_event.data())) {
    return fail(errc::type_mismatch, "proto_data carries a binary payload", "data");
  }
  return encode_event(cloud_event, any_url{type_url});
}

// spec: SWR-PROTO-0024
[[nodiscard]] inline auto encode_events(std::span<const event> events) -> binary {
  binary out;
  for (const auto& cloud_event : events) {
    put_field(out, batch_field::events, encode_event(cloud_event, proto_type_url(cloud_event)));
  }
  return out;
}

}  // namespace detail

}  // namespace ce::inline v4::protobuf

// spec: SWR-BUILD-0005
namespace ce::inline v4 {

// spec: SYS-PROTO-0001
// spec: SWR-PROTO-0001
struct protobuf_format {
  // spec: SWR-PROTO-0002
  static constexpr std::string_view content_type = protobuf::content_type;

  // spec: SWR-PROTO-0024
  static constexpr std::string_view batch_content_type = protobuf::batch_content_type;

  [[nodiscard]] static auto encode(const event& cloud_event) -> result<binary> {
    return protobuf::detail::encode_event(cloud_event,
                                          protobuf::detail::proto_type_url(cloud_event));
  }

  [[nodiscard]] static auto decode(std::span<const std::byte> encoded,
                                   protobuf::decode_options options = {}) -> result<event> {
    return protobuf::detail::decode_event(encoded, options);
  }

  // spec: SWR-PROTO-0024
  [[nodiscard]] static auto encode_batch(std::span<const event> events) -> result<binary> {
    return protobuf::detail::encode_events(events);
  }

  // spec: SWR-PROTO-0024
  [[nodiscard]] static auto decode_batch(std::span<const std::byte> encoded,
                                         protobuf::decode_options options = {})
      -> result<std::vector<event>> {
    return protobuf::detail::decode_events(encoded, options);
  }

  // spec: SWR-PROTO-0025
  template<json::json_codec Codec>
  [[nodiscard]] static auto decode_as(std::span<const std::byte> encoded,
                                      protobuf::decode_options options = {})
      -> result<protobuf::decoded_json<Codec>> {
    auto read = protobuf::detail::decode_event(encoded, options);
    if (!read) {
      return protobuf::detail::propagate(std::move(read).error());
    }
    auto payload = protobuf::detail::parse_json_payload<Codec>(*read);
    if (!payload) {
      return protobuf::detail::propagate(std::move(payload).error());
    }
    return protobuf::decoded_json<Codec>{.event = std::move(*read), .payload = std::move(*payload)};
  }

  // spec: SWR-PROTO-0026
  [[nodiscard]] static auto decode_detailed(std::span<const std::byte> encoded,
                                            protobuf::decode_options options = {})
      -> result<protobuf::detailed_event> {
    return protobuf::detail::decode_detailed_event(encoded, options);
  }

  // spec: SWR-PROTO-0026
  [[nodiscard]] static auto encode_proto_data(const event& cloud_event, std::string_view type_url)
      -> result<binary> {
    return protobuf::detail::encode_event_as_any(cloud_event, type_url);
  }
};

}  // namespace ce::inline v4
