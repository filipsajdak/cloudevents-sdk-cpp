#pragma once

#include <string>
#include <utility>
#include <variant>

#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

// spec: SWR-EXT-0006
namespace ce::inline v3 {

namespace detail {
template<described T, json::json_codec Codec>
[[nodiscard]] auto payload_from(const typename Codec::value& document) -> result<T> {
  auto decoded = from_json_value<Codec, T>(document);
  if (!decoded) {
    return fail(decoded.error().code,
                decoded.error().detail,
                decoded.error().where.empty() ? std::string{"data"} : decoded.error().where);
  }
  return decoded;
}
}  // namespace detail

// spec: SWR-EXT-0005
template<described T, json::json_codec Codec>
[[nodiscard]] auto data_as(const event& cloud_event) -> result<T> {
  const std::string* text = nullptr;
  std::string converted;

  if (const auto* stored = std::get_if<json_text>(&cloud_event.data())) {
    text = &stored->raw;
  } else if (const auto* document = std::get_if<json_document>(&cloud_event.data())) {
    // spec: SWR-EXT-0012
    if (const auto* own = document->get<Codec>(); own != nullptr) {
      return detail::payload_from<T, Codec>(*own);
    }
    // spec: SWR-JSON-0042
    converted = document->dump();
    text = &converted;
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

  return detail::payload_from<T, Codec>(*document);
}

// spec: SWR-EXT-0011
template<described T, json::json_codec Codec>
void set_data(event& cloud_event, const T& value) {
  using namespace ce::literals;
  cloud_event.set_data(json_document::make<Codec>(to_json_value<Codec>(value)),
                       "application/json"_mediatype);
}

// spec: SWR-EXT-0004
template<described T, json::json_codec Codec>
class event_of {
 public:
  using payload_type = T;
  using codec_type = Codec;

  explicit event_of(event cloud_event) : event_{std::move(cloud_event)} {}

  [[nodiscard]] static auto with_data(event cloud_event, const T& value) -> event_of {
    ce::v3::set_data<T, Codec>(cloud_event, value);
    return event_of{std::move(cloud_event)};
  }

  [[nodiscard]] auto data() const -> result<T> { return data_as<T, Codec>(event_); }

  void set_data(const T& value) { ce::v3::set_data<T, Codec>(event_, value); }

  [[nodiscard]] auto underlying() const noexcept -> const event& { return event_; }

  friend auto operator==(const event_of&, const event_of&) -> bool = default;

 private:
  event event_;
};

}  // namespace ce::inline v3
