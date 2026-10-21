#pragma once

/// \file
/// \brief What every binding's moving entry point promises about the message it is given.
///
/// HTTP, Kafka and NATS share the binding core, so one set of checks drives all three
/// through a stand-in that names the binding's own entry points. The stand-in `Bind` has:
///
///   static constexpr std::string_view prefix        the attribute prefix ("ce-", "ce_")
///   static constexpr std::string_view media_header  the field that carries the media type
///   static auto lay_out(const ce::event&, ce::content_mode) -> ce::result<ce::message>
///   static auto copying(const ce::message&, ce::json::decode_options) -> ce::result<ce::event>
///   static auto moving(ce::message&&, ce::json::decode_options) -> ce::result<ce::event>
///
/// (CR-0005: SWR-BIND-0014 to SWR-BIND-0017.)

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/decode_options.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

namespace ce_test::moving {

/// A body small enough that the headers dominate it.
inline constexpr std::size_t small_body_bytes = 256;
/// A body large enough that its copy dominates a receive: the size the perf operations use.
inline constexpr std::size_t large_body_bytes = std::size_t{64} * 1024;
/// The capacity an empty body is given, so that a call which keeps it can be seen to.
inline constexpr std::size_t reserved_empty_bytes = 64;
/// Capacity a body holds beyond its size, so that releasing it is not mistaken for a tight fit.
inline constexpr std::size_t spare_capacity_bytes = 64;
/// An arbitrary non-zero octet, so a body is not a run of zeros.
inline constexpr std::byte body_fill{0x5A};

inline constexpr std::size_t body_sizes[] = {0, small_body_bytes, large_body_bytes};
inline constexpr std::size_t non_empty_body_sizes[] = {small_body_bytes, large_body_bytes};

/// The media types under which a body is moved, and the absence of one (an empty name).
inline constexpr std::string_view moved_media_types[] = {
    "application/octet-stream", "text/plain", ""};

inline constexpr std::string_view json_text_body = R"({"a":[1,2],"b":{"c":null}})";
inline constexpr std::string_view malformed_json_body = "{not json";

/// What a message was before a call: enough to say whether the call touched it, down to
/// the buffer the body lives in.
struct snapshot {
  ce::raw_headers headers;
  ce::binary body;
  const std::byte* address;
  std::size_t capacity;
};

[[nodiscard]] inline auto take(const ce::message& subject) -> snapshot {
  return snapshot{.headers = subject.header_fields,
                  .body = subject.body,
                  .address = subject.body.data(),
                  .capacity = subject.body.capacity()};
}

/// Equal in the headers, the bytes, the buffer's address and its capacity.
[[nodiscard]] inline auto unchanged(const ce::message& subject, const snapshot& before) -> bool {
  return subject.header_fields == before.headers && subject.body == before.body &&
         subject.body.data() == before.address && subject.body.capacity() == before.capacity;
}

[[nodiscard]] inline auto media_type_of(std::string_view name)
    -> std::optional<ce::datacontenttype> {
  if (name.empty()) {
    return std::nullopt;
  }
  auto made = ce::datacontenttype::make(name);
  return made ? std::optional{*made} : std::nullopt;
}

[[nodiscard]] inline auto body_event(std::string_view media_type, ce::data_t data) -> ce::event {
  using namespace ce::literals;
  return ce::event{"1"_id,
                   "/spec/test"_source,
                   "com.example.thing"_type,
                   {.datacontenttype = media_type_of(media_type), .data = std::move(data)}};
}

[[nodiscard]] inline auto bytes_event(std::string_view media_type, std::size_t size) -> ce::event {
  return body_event(media_type, ce::binary(size, body_fill));
}

[[nodiscard]] inline auto json_event() -> ce::event {
  return body_event("application/json", ce::json_text{.raw = std::string{json_text_body}});
}

/// Both results are failures with one code, or successes holding equal events.
[[nodiscard]] inline auto agree(const ce::result<ce::event>& left,
                                const ce::result<ce::event>& right) -> bool {
  if (left.has_value() != right.has_value()) {
    return false;
  }
  if (left.has_value()) {
    return bool{*left == *right};
  }
  return left.error().code == right.error().code;
}

[[nodiscard]] inline auto carries_bytes(const ce::result<ce::event>& decoded,
                                        const std::byte* at,
                                        std::size_t size) -> bool {
  if (!decoded) {
    return false;
  }
  const auto* held = std::get_if<ce::binary>(&decoded->data());
  return held != nullptr && held->data() == at && held->size() == size;
}

/// \brief A moving call returns what the copying call returns (SWR-BIND-0014).
///
/// Every body kind: moved ones (under each media type and none, at three sizes), a JSON body
/// parsed and unparsed, an empty one, and a structured message.
template<class Bind>
void check_moving_agrees_with_copying(std::string_view label) {
  using namespace boost::ut;
  const auto check = [&label](const ce::result<ce::message>& laid_out, std::string_view what) {
    expect(laid_out.has_value()) << label << ": " << what;
    if (!laid_out) {
      return;
    }
    for (const auto& options : {ce::json::decode_options{},
                                ce::json::decode_options{.parse_binary_body = false},
                                ce::json::decode_options{.retain_document_up_to = 0}}) {
      ce::message given = *laid_out;
      const auto by_copy = Bind::copying(*laid_out, options);
      const auto by_move = Bind::moving(std::move(given), options);
      expect(agree(by_copy, by_move)) << label << ": " << what;
    }
  };

  for (const auto media_type : moved_media_types) {
    for (const std::size_t size : body_sizes) {
      check(Bind::lay_out(bytes_event(media_type, size), ce::content_mode::binary_mode),
            std::string{media_type} + " of " + std::to_string(size) + " bytes");
    }
  }
  check(Bind::lay_out(json_event(), ce::content_mode::binary_mode), "a JSON body");
  check(Bind::lay_out(json_event(), ce::content_mode::structured), "a structured message");
}

/// \brief A moved body is empty with no capacity, adopted without a copy, and the headers stay
/// (SWR-BIND-0014, SWR-BIND-0015).
template<class Bind>
void check_moved_body_is_released(std::string_view label) {
  using namespace boost::ut;

  for (const auto media_type : moved_media_types) {
    for (const std::size_t size : non_empty_body_sizes) {
      auto laid_out = Bind::lay_out(bytes_event(media_type, size), ce::content_mode::binary_mode);
      expect(laid_out.has_value()) << label;
      if (!laid_out) {
        continue;
      }
      for (const bool unparsed : {false, true}) {
        ce::message given = *laid_out;
        const auto before = take(given);
        const auto decoded = Bind::moving(std::move(given),
                                          ce::json::decode_options{.parse_binary_body = !unparsed});
        expect(decoded.has_value()) << label << ": " << media_type << " " << size;
        // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
        expect(given.body.size() == 0U) << label << ": " << media_type << " " << size;
        expect(given.body.capacity() == 0U) << label << ": the buffer is released";
        expect(given.header_fields == before.headers) << label << ": the headers are unchanged";
        // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
        expect(carries_bytes(decoded, before.address, size))
            << label << ": " << media_type << " " << size << " the event adopted the buffer";
      }
    }
  }
}

/// \brief A body that is not moved leaves the message byte for byte as it was (SWR-BIND-0015).
template<class Bind>
void check_unmoved_body_is_left_intact(std::string_view label) {
  using namespace boost::ut;

  const auto left_intact =
      [&label](ce::message given, ce::json::decode_options options, std::string_view what) {
        const auto before = take(given);
        const auto decoded = Bind::moving(std::move(given), options);
        expect(decoded.has_value()) << label << ": " << what;
        // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
        expect(unchanged(given, before)) << label << ": " << what;
      };

  const auto json = Bind::lay_out(json_event(), ce::content_mode::binary_mode);
  const auto structured = Bind::lay_out(json_event(), ce::content_mode::structured);
  expect(json.has_value() && structured.has_value()) << label;
  if (!json || !structured) {
    return;
  }
  left_intact(*json, {}, "a JSON body, parsed");
  left_intact(*json, {.parse_binary_body = false}, "a JSON body, unparsed");
  left_intact(*json, {.retain_document_up_to = 0}, "a JSON body kept as text");
  left_intact(*structured, {}, "a structured message");

  for (const auto media_type : moved_media_types) {
    auto empty = Bind::lay_out(bytes_event(media_type, 0), ce::content_mode::binary_mode);
    expect(empty.has_value()) << label;
    if (empty) {
      // An empty body is not moved, so even the capacity it had stays.
      empty->body.reserve(reserved_empty_bytes);
      left_intact(*empty, {}, "an empty body");
    }
  }
}

/// \brief A failed moving call leaves the message unchanged, for every body (SWR-BIND-0016).
///
/// The failures: a message that is no CloudEvent, an attribute that does not parse, and a message
/// whose final validation fails (no id), each over a body that would be moved; and a malformed
/// JSON body, which the parse refuses.
template<class Bind>
void check_failed_call_leaves_the_message(std::string_view label) {
  using namespace boost::ut;

  const auto refused = [&label](ce::message given, ce::errc code, std::string_view what) {
    const auto before = take(given);
    const auto by_copy = Bind::copying(given, {});
    const auto decoded = Bind::moving(std::move(given), {});
    expect(!decoded.has_value()) << label << ": " << what;
    if (!decoded) {
      expect(decoded.error().code == code) << label << ": " << what;
    }
    expect(agree(by_copy, decoded)) << label << ": " << what;
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(unchanged(given, before)) << label << ": " << what;
  };

  const std::string prefix{Bind::prefix};
  const std::string media_header{Bind::media_header};
  for (const auto media_type : moved_media_types) {
    for (const std::size_t size : non_empty_body_sizes) {
      const std::string what = std::string{media_type} + " of " + std::to_string(size) + " bytes";
      const auto with_media = [&](ce::raw_headers fields) {
        if (!media_type.empty()) {
          fields.add(media_header, std::string{media_type});
        }
        return ce::message{.header_fields = std::move(fields), .body = ce::binary(size, body_fill)};
      };
      refused(
          with_media(
              {{prefix + "specversion", "1.0"}, {prefix + "source", "/s"}, {prefix + "type", "t"}}),
          ce::errc::missing_required_attribute,
          "no id: " + what);
      refused(with_media({{prefix + "id", "1"}, {prefix + "source", "/s"}, {prefix + "type", "t"}}),
              ce::errc::not_a_cloudevent,
              "no specversion: " + what);
      refused(with_media({{prefix + "specversion", "1.0"},
                          {prefix + "id", "1"},
                          {prefix + "source", "/s"},
                          {prefix + "type", "t"},
                          {prefix + "time", "yesterday"}}),
              ce::errc::invalid_timestamp,
              "an attribute that does not parse: " + what);
    }
  }

  auto json = Bind::lay_out(json_event(), ce::content_mode::binary_mode);
  auto structured = Bind::lay_out(json_event(), ce::content_mode::structured);
  expect(json.has_value() && structured.has_value()) << label;
  if (json && structured) {
    json->body = ce::to_bytes(malformed_json_body);
    refused(*json, ce::errc::parse_error, "a malformed JSON body");
    structured->body = ce::to_bytes(malformed_json_body);
    refused(*structured, ce::errc::parse_error, "a malformed structured body");
  }
}

/// \brief The copying entry point never modifies its message (SWR-BIND-0017).
///
/// Whether it is handed an lvalue, a const reference or `std::move(message)`, on a body a moving
/// call would move, on one it would not, and on one it refuses.
template<class Bind>
void check_copying_entry_point_leaves_the_message(std::string_view label) {
  using namespace boost::ut;

  const auto left_alone = [&label](const ce::message& original, std::string_view what) {
    ce::message given = original;
    const auto before = take(given);
    const auto by_lvalue = Bind::copying(given, {});
    expect(unchanged(given, before)) << label << ": " << what << " by lvalue";
    const ce::message& by_const = given;
    const auto by_reference = Bind::copying(by_const, {});
    expect(unchanged(given, before)) << label << ": " << what << " by const reference";
    // The pin of SWR-BIND-0017: a std::move to a const reference must leave the message alone.
    // NOLINTNEXTLINE(performance-move-const-arg)
    const auto by_move = Bind::copying(std::move(given), {});
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(unchanged(given, before)) << label << ": " << what << " by std::move";
    expect(agree(by_lvalue, by_reference) && agree(by_lvalue, by_move)) << label << ": " << what;
  };

  for (const auto media_type : moved_media_types) {
    for (const std::size_t size : body_sizes) {
      auto laid_out = Bind::lay_out(bytes_event(media_type, size), ce::content_mode::binary_mode);
      expect(laid_out.has_value()) << label;
      if (laid_out) {
        left_alone(*std::move(laid_out), std::string{media_type} + " of " + std::to_string(size));
      }
    }
  }
  auto json = Bind::lay_out(json_event(), ce::content_mode::binary_mode);
  auto structured = Bind::lay_out(json_event(), ce::content_mode::structured);
  expect(json.has_value() && structured.has_value()) << label;
  if (json && structured) {
    left_alone(*json, "a JSON body");
    left_alone(*structured, "a structured message");
    json->body = ce::to_bytes(malformed_json_body);
    left_alone(*json, "a malformed JSON body");
  }
}

}  // namespace ce_test::moving
