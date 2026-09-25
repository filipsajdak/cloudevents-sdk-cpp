#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
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

// spec: SWR-EXT-0007
template<class T>
struct decoded {
  ce::v3::event event;
  T payload;
};

namespace detail {
template<described T, json::json_codec Codec>
[[nodiscard]] auto typed_read(event&& cloud_event, const typename Codec::value* json_member)
    -> result<decoded<T>> {
  auto payload = json_member != nullptr ? payload_from<T, Codec>(*json_member)
                                        : data_as<T, Codec>(cloud_event);
  if (!payload) {
    return fail(payload.error().code, payload.error().detail, payload.error().where);
  }
  return decoded<T>{.event = std::move(cloud_event), .payload = std::move(*payload)};
}
}  // namespace detail

// spec: SWR-EXT-0007
template<described T, json::json_codec Codec>
[[nodiscard]] auto decode_as(std::string_view text, json::decode_options options = {})
    -> result<decoded<T>> {
  return json::detail::typed_entry<Codec>::template decode<decoded<T>>(
      text, options, detail::typed_read<T, Codec>);
}

// spec: SWR-EXT-0008
template<described T, json::json_codec Codec>
[[nodiscard]] auto decode_batch_as(std::string_view text, json::decode_options options = {})
    -> result<std::vector<decoded<T>>> {
  return json::detail::typed_entry<Codec>::template decode_batch<decoded<T>>(
      text, options, detail::typed_read<T, Codec>);
}

// spec: SWR-EXT-0009
template<described T, json::json_codec Codec>
[[nodiscard]] auto from_value_as(const typename Codec::value& document) -> result<decoded<T>> {
  return json::detail::typed_entry<Codec>::template from_value<decoded<T>>(
      document, detail::typed_read<T, Codec>);
}

// spec: SWR-EXT-0010
template<described T, json::json_codec Codec>
[[nodiscard]] auto encode_as(const event& cloud_event, const T& payload) -> result<std::string> {
  std::string_view media_type = "application/json";
  if (const auto& declared = cloud_event.datacontenttype(); declared) {
    if (!is_json_content_type(declared->view())) {
      return fail(errc::type_mismatch,
                  "a typed payload is JSON but datacontenttype says otherwise",
                  "datacontenttype");
    }
    media_type = declared->view();
  }
  auto document = json::detail::typed_entry<Codec>::to_value(
      cloud_event, media_type, to_json_value<Codec>(payload));
  if (!document) {
    return fail(document.error().code, document.error().detail, document.error().where);
  }
  return Codec::dump(*document);
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
