#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/binding/detail/percent.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/decode_options.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

// spec: SYS-HTTP-0001
namespace ce::inline v4::http {

namespace detail {

inline constexpr std::string_view attribute_prefix = "ce-";
inline constexpr std::string_view content_type_header = "Content-Type";

using ce::v4::to_bytes;
using ce::v4::to_text;
using ce::v4::detail::is_valid_utf8;
using ce::v4::detail::starts_with_ignoring_case;
using ce::v4::binding::detail::hex_digit;
using ce::v4::binding::detail::hex_value;
using ce::v4::binding::detail::needs_escape;
using ce::v4::binding::detail::percent_decode;
using ce::v4::binding::detail::percent_encode;
using ce::v4::binding::detail::text_of;

inline constexpr unsigned char first_printable_ascii = 0x20U;
inline constexpr unsigned char delete_character = 0x7FU;

// spec: SWR-BIND-0004
template <class Values>
struct http_traits {
  static constexpr std::string_view attribute_prefix = ce::v4::http::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v4::http::detail::content_type_header;
  static constexpr bool case_sensitive_names = false;

  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    return Values::encode(text);
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> result<std::string> {
    return Values::decode(text);
  }
};

}  // namespace detail

template <class T>
concept value_policy = requires(std::string_view text) {
  { T::encode(text) } -> std::same_as<result<std::string>>;
  { T::decode(text) } -> std::same_as<result<std::string>>;
};

// spec: SWR-HTTP-0010
// spec: SWR-HTTP-0011
// spec: SWR-HTTP-0012
// spec: SWR-HTTP-0017
struct percent_encoded_values {
  [[nodiscard]] static auto encode(std::string_view text) -> result<std::string> {
    if (!detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "an attribute value must be well-formed UTF-8");
    }
    return detail::percent_encode(text);
  }

  [[nodiscard]] static auto decode(std::string_view text) -> result<std::string> {
    return detail::percent_decode(text);
  }
};

// spec: SWR-HTTP-0016
struct literal_values {
  [[nodiscard]] static auto encode(std::string_view text) -> result<std::string> {
    for (const char character : text) {
      if (static_cast<unsigned char>(character) < detail::first_printable_ascii ||
          static_cast<unsigned char>(character) == detail::delete_character) {
        return fail(errc::invalid_argument,
                    "a literal header value may not contain a control character");
      }
    }
    if (!detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "a header value must be well-formed UTF-8");
    }
    return std::string{text};
  }

  [[nodiscard]] static auto decode(std::string_view text) -> result<std::string> {
    if (!detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "a header value must be well-formed UTF-8");
    }
    return std::string{text};
  }
};

static_assert(binding::binding_traits<detail::http_traits<percent_encoded_values>>);
static_assert(binding::binding_traits<detail::http_traits<literal_values>>);

// spec: SWR-HTTP-0006
[[nodiscard]] inline auto detect_content_mode(const message& request) -> content_mode {
  const std::string* declared = request.header_fields.find(detail::content_type_header);
  if (declared == nullptr) {
    return content_mode::binary_mode;
  }
  if (detail::starts_with_ignoring_case(*declared, "application/cloudevents-batch")) {
    return content_mode::batched;
  }
  if (detail::starts_with_ignoring_case(*declared, "application/cloudevents")) {
    return content_mode::structured;
  }
  return content_mode::binary_mode;
}

// spec: SWR-HTTP-0003
// spec: SWR-HTTP-0007
// spec: SWR-HTTP-0008
// spec: SWR-HTTP-0009
template <json::json_codec Codec, value_policy Values = percent_encoded_values>
[[nodiscard]] auto to_message(const event& cloud_event, content_mode mode) -> result<message> {
  if (mode == content_mode::structured) {
    return binding::encode_structured<detail::http_traits<Values>, Codec>(cloud_event);
  }

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "a batch carries several events; use to_batch_message");
  }

  message out;
  if (auto written =
          binding::write_attributes<detail::http_traits<Values>>(cloud_event, out.header_fields);
      !written) {
    return ce::v4::detail::forward_failure(std::move(written).error());
  }
  binding::write_body(cloud_event, out);
  return out;
}

// spec: SWR-HTTP-0005
template <json::json_codec Codec>
[[nodiscard]] auto to_batch_message(std::span<const event> events) -> result<message> {
  auto text = json_format<Codec>::encode_batch(events);
  if (!text) {
    return ce::v4::detail::forward_failure(std::move(text).error());
  }
  message out;
  out.header_fields.set(std::string{detail::content_type_header},
                        std::string{json::batch_content_type});
  out.body = detail::to_bytes(*text);
  return out;
}

// spec: SWR-HTTP-0004
// spec: SWR-HTTP-0013
// spec: SWR-HTTP-0014
// spec: SWR-HTTP-0015
// spec: SWR-BIND-0009
template<json::json_codec Codec, value_policy Values = percent_encoded_values>
[[nodiscard]] auto from_message(const message& request, json::decode_options options = {})
    -> result<event> {
  const content_mode mode = detect_content_mode(request);

  if (mode == content_mode::structured) {
    return binding::decode_structured<Codec>(request, options);
  }
  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "this message is a batch; use from_batch_message");
  }

  if (!request.header_fields.contains("ce-specversion")) {
    return fail(errc::not_a_cloudevent, "no ce-specversion header and no CloudEvents content type");
  }

  auto under_construction =
      binding::read_attributes<detail::http_traits<Values>>(request.header_fields);
  if (!under_construction) {
    return ce::v4::detail::forward_failure(std::move(under_construction).error());
  }

  if (const std::string* declared = request.header_fields.find(detail::content_type_header);
      declared != nullptr) {
    auto media_type = datacontenttype::make(*declared);
    if (!media_type) {
      return ce::v4::detail::forward_failure(std::move(media_type).error());
    }
    under_construction->rest.datacontenttype = std::move(*media_type);
  }

  auto payload =
      binding::read_body<Codec>(request.body, under_construction->rest.datacontenttype, options);
  if (!payload) {
    return ce::v4::detail::forward_failure(std::move(payload).error());
  }
  under_construction->rest.data = std::move(*payload);

  return std::move(*under_construction).build();
}

// spec: SWR-BIND-0009
template<json::json_codec Codec>
[[nodiscard]] auto from_batch_message(const message& request, json::decode_options options = {})
    -> result<std::vector<event>> {
  if (detect_content_mode(request) != content_mode::batched) {
    return fail(errc::not_a_cloudevent, "the content type is not a CloudEvents batch");
  }
  return json_format<Codec>::decode_batch(detail::text_of(request.body), options);
}

}  // namespace ce::inline v4::http
