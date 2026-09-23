#pragma once

#include <string>
#include <utility>
#include <variant>

#include <cloudevents/v2/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

namespace ce::v2 {

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

template<described T, json::json_codec Codec>
void set_data(event& cloud_event, const T& value) {
  using namespace ce::v2::literals;
  const auto document = to_json_value<Codec>(value);
  cloud_event.set_data(json_text{.raw = Codec::dump(document)}, "application/json"_mediatype);
}

template<described T, json::json_codec Codec>
class event_of {
 public:
  using payload_type = T;
  using codec_type = Codec;

  explicit event_of(event cloud_event) : event_{std::move(cloud_event)} {}

  [[nodiscard]] static auto with_data(event cloud_event, const T& value) -> event_of {
    ce::v2::set_data<T, Codec>(cloud_event, value);
    return event_of{std::move(cloud_event)};
  }

  [[nodiscard]] auto data() const -> result<T> { return data_as<T, Codec>(event_); }

  void set_data(const T& value) { ce::v2::set_data<T, Codec>(event_, value); }

  [[nodiscard]] auto underlying() const noexcept -> const event& { return event_; }

  friend auto operator==(const event_of&, const event_of&) -> bool = default;

 private:
  event event_;
};

}  // namespace ce::v2
