#pragma once

/// \file
/// \brief Format rules every codec must satisfy, written once for two callers.
///
/// The test suite and `bench/codec_check.cpp` both need these, and they cannot
/// share a test framework: a benchmark codec such as Glaze needs C++23 and
/// cannot live in the test tree at all. So the checks take a reporting callback
/// and each caller supplies its own, which is what lets a codec that ships and a
/// codec that is only measured be judged by the same rules.
///
/// `report(bool ok, std::string_view what)`.

#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

namespace ce::checks {

/// \brief A document whose time carries nanoseconds and a non-UTC offset.
[[nodiscard]] inline auto timestamp_document() -> std::string {
  return R"({"specversion":"1.0","id":"1","source":"/s","type":"t",)"
         R"("time":"2026-09-20T12:34:56.123456789+02:00"})";
}

/// \brief A subject carrying U+1F600 as a surrogate PAIR.
///
/// Built at run time from the escape character, because a `\u` escape written
/// into a C++ source literal is decoded by the compiler before the codec ever
/// sees it, and the check would then pass without testing anything.
[[nodiscard]] inline auto surrogate_pair_document() -> std::string {
  const char backslash = static_cast<char>(92);
  std::string out = R"({"specversion":"1.0","id":"1","source":"/s","type":"t","subject":")";
  out += backslash;
  out += "uD83D";
  out += backslash;
  out += "uDE00";
  out += R"("})";
  return out;
}

/// \brief A batch of `count` events.
[[nodiscard]] inline auto batch_document(std::size_t count) -> std::string {
  std::string out = "[";
  for (std::size_t index = 0; index < count; ++index) {
    if (index != 0) {
      out += ',';
    }
    out += R"({"specversion":"1.0","id":")";
    out += std::to_string(index);
    out += R"(","source":"/s","type":"com.example.batch"})";
  }
  out += ']';
  return out;
}

/// \brief A timestamp survives the format byte for byte, offset included.
///
/// A codec that routes the string through a date type, or that normalises to
/// UTC, loses the offset the sender chose. SWR-CORE-0009 requires it kept, and
/// nothing else in the format layer would notice.
template <json::json_codec Codec, class Report>
void check_timestamp_is_byte_exact(Report report) {
  using format = json_format<Codec>;

  auto decoded = format::decode(timestamp_document());
  report(decoded.has_value(), "the timestamp document does not decode");
  if (!decoded) {
    return;
  }
  const auto& when = decoded->time();
  report(when.has_value(), "the time attribute was lost");
  if (when) {
    report(to_string(*when) == "2026-09-20T12:34:56.123456789+02:00",
           "the timestamp did not round-trip byte for byte");
  }

  auto encoded = format::encode(*decoded);
  report(encoded.has_value(), "the decoded event does not re-encode");
  if (encoded) {
    report(encoded->find("2026-09-20T12:34:56.123456789+02:00") != std::string::npos,
           "the re-encoded document lost the original timestamp text");
  }
}

/// \brief A non-BMP character arriving as a surrogate pair becomes valid UTF-8.
///
/// Some producers emit U+1F600 as the escape pair uD83D then uDE00. A codec
/// that decodes each half on its own produces two unpaired surrogates, which is
/// not well-formed UTF-8, and the damage only shows up at whatever reads the
/// value next.
///
/// The escapes are named rather than written here: this comment is exactly the
/// place a reader would paste them, and a literal escape in the source is
/// decoded before it means anything.
template <json::json_codec Codec, class Report>
void check_surrogate_pair(Report report) {
  using format = json_format<Codec>;

  auto decoded = format::decode(surrogate_pair_document());
  report(decoded.has_value(), "a surrogate pair does not decode");
  if (!decoded || !decoded->subject()) {
    report(false, "the subject carrying a surrogate pair was lost");
    return;
  }
  // U+1F600 is four bytes in UTF-8: F0 9F 98 80.
  report(decoded->subject()->view() == "\xF0\x9F\x98\x80",
         "a surrogate pair did not become one UTF-8 character");
}

/// \brief A hundred events survive a batch round trip.
///
/// One event proves the mapping; a hundred proves the codec's array handling,
/// which is where a reused parser state or an invalidated pointer shows up.
template <json::json_codec Codec, class Report>
void check_batch_round_trip(Report report) {
  using format = json_format<Codec>;

  const std::string document = batch_document(100);
  auto decoded = format::decode_batch(document);
  report(decoded.has_value(), "the batch does not decode");
  if (!decoded) {
    return;
  }
  report(decoded->size() == 100, "the batch lost events");
  if (decoded->size() != 100) {
    return;
  }
  report(decoded->front().id().view() == "0" && decoded->back().id().view() == "99",
         "the batch decoded in the wrong order");

  auto encoded = format::encode_batch(*decoded);
  report(encoded.has_value(), "the batch does not re-encode");
  if (!encoded) {
    return;
  }
  auto again = format::decode_batch(*encoded);
  report(again.has_value(), "the re-encoded batch does not decode");
  if (again) {
    report(again->size() == 100, "the batch lost events on the second trip");
    report(*again == *decoded, "the batch round trip is not identity");
  }
}

/// \brief Every check above, for one codec.
template <json::json_codec Codec, class Report>
void check_format_rules(Report report) {
  check_timestamp_is_byte_exact<Codec>(report);
  check_surrogate_pair<Codec>(report);
  check_batch_round_trip<Codec>(report);
}

}  // namespace ce::checks
