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
[[nodiscard]] auto data_as(const event& subject) -> result<T> {
  const std::string* text = nullptr;

  if (const auto* stored = std::get_if<json_text>(&subject.data)) {
    text = &stored->raw;
  } else if (const auto* plain = std::get_if<std::string>(&subject.data)) {
    if (!subject.datacontenttype || !is_json_content_type(*subject.datacontenttype)) {
      return fail(errc::type_mismatch,
                  "the payload is text but datacontenttype does not say it is JSON",
                  "data");
    }
    text = plain;
  } else if (std::holds_alternative<std::monostate>(subject.data)) {
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
template<described T, json::json_codec Codec>
auto set_data(event& subject, const T& value) -> result<void> {
  auto document = to_json_value<Codec>(value);
  subject.data = json_text{.raw = Codec::dump(document)};
  subject.datacontenttype = "application/json";
  return {};
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

  event_of() = default;
  explicit event_of(event subject) : event_{std::move(subject)} {}

  /// \brief Build a typed view whose payload is already written.
  [[nodiscard]] static auto with_data(event subject, const T& value) -> result<event_of> {
    if (auto stored = ce::v1::set_data<T, Codec>(subject, value); !stored) {
      return fail(stored.error().code, stored.error().detail, stored.error().where);
    }
    return event_of{std::move(subject)};
  }

  [[nodiscard]] auto data() const -> result<T> { return data_as<T, Codec>(event_); }

  auto set_data(const T& value) -> result<void> {
    return ce::v1::set_data<T, Codec>(event_, value);
  }

  [[nodiscard]] auto underlying() const noexcept -> const event& { return event_; }
  [[nodiscard]] auto underlying() noexcept -> event& { return event_; }

  [[nodiscard]] auto validate() const -> result<void> { return event_.validate(); }

  friend auto operator==(const event_of&, const event_of&) -> bool = default;

 private:
  event event_{};
};

}  // namespace ce::inline v1
