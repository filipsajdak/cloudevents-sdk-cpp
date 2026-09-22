#pragma once

/// \file
/// \brief Typed access to an event payload. In the format layer, because core
/// may not name a codec (SWR-EXT-0006).

#include <string>
#include <utility>
#include <variant>

#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v1 {

/// \brief Read the event payload as a described type.
///
/// The payload must be JSON: either `json_text`, or a string whose
/// `datacontenttype` says it is JSON. Bytes are refused rather than guessed at.
template<described T, json::json_codec Codec>
[[nodiscard]] auto data_as(const event& cloud_event) -> result<T> {
  const std::string* text = nullptr;

  if (const auto* stored = std::get_if<json_text>(&cloud_event.data())) {
    text = &stored->raw;
  } else if (const auto* plain = std::get_if<std::string>(&cloud_event.data())) {
    const auto& media_type = cloud_event.datacontenttype();
    if (!media_type || !is_json_content_type(media_type->view())) {
      return fail(errc::type_mismatch,
                  "the payload is text but datacontenttype does not say it is JSON",
                  "data");
    }
    text = plain;
  } else if (std::holds_alternative<std::monostate>(cloud_event.data())) {
    return fail(errc::missing_required_attribute, "the event carries no payload", "data");
  } else {
    return fail(errc::type_mismatch, "a binary payload is not a described type", "data");
  }

  auto document = Codec::parse(*text);
  if (!document) {
    return fail(document.error().code, document.error().detail, "data");
  }

  auto decoded = from_json_value<Codec, T>(*document);
  if (!decoded) {
    return fail(decoded.error().code,
                decoded.error().detail,
                decoded.error().where.empty() ? std::string{"data"} : decoded.error().where);
  }
  return decoded;
}

/// \brief Write a described type as the event payload.
///
/// The payload is stored as `json_text` and `datacontenttype` is set to
/// `application/json`, so the event states what it carries.
///
/// Returns nothing. It used to return `result<void>` and ended `return {};` on
/// every path, so every caller wrote error handling for a failure that could not
/// happen. `to_json_value` is total over a described type - the describe seam
/// refuses anything else with a static_assert - and dumping a DOM the codec just
/// built cannot fail either.
template<described T, json::json_codec Codec>
void set_data(event& cloud_event, const T& value) {
  using namespace ce::literals;
  auto document = to_json_value<Codec>(value);
  cloud_event.set_data(json_text{.raw = Codec::dump(document)}, "application/json"_mediatype);
}

/// \brief A typed view over an event: the payload type is part of the type.
///
/// A view, not a container: it holds the event and hands it back unchanged, so a
/// producer and a consumer share a compile-time contract about the payload
/// rather than an agreement recorded only in prose (SWR-EXT-0004).
template<described T, json::json_codec Codec>
class event_of {
 public:
  using payload_type = T;
  using codec_type = Codec;

  explicit event_of(event cloud_event) : event_{std::move(cloud_event)} {}

  /// \brief Build a typed view whose payload is already written.
  [[nodiscard]] static auto with_data(event cloud_event, const T& value) -> event_of {
    ce::v1::set_data<T, Codec>(cloud_event, value);
    return event_of{std::move(cloud_event)};
  }

  [[nodiscard]] auto data() const -> result<T> { return data_as<T, Codec>(event_); }

  void set_data(const T& value) { ce::v1::set_data<T, Codec>(event_, value); }

  /// Const only. A mutable reference let a caller replace the payload with one
  /// of another type, which is the compile-time contract this class exists to
  /// hold - breakable with no cast and nothing to notice it.
  [[nodiscard]] auto underlying() const noexcept -> const event& { return event_; }

  friend auto operator==(const event_of&, const event_of&) -> bool = default;

 private:
  event event_;
};

}  // namespace ce::inline v1
