#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/binding/detail/percent.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

// spec: SYS-NATS-0001
// spec: SWR-NATS-0003
namespace ce::inline v3::nats {

// spec: SWR-NATS-0001
// spec: SWR-NATS-0002
template <json::json_codec Codec>
[[nodiscard]] auto to_payload(const event& cloud_event) -> result<std::string> {
  return json_format<Codec>::encode(cloud_event);
}

// spec: SWR-NATS-0004
template <json::json_codec Codec>
[[nodiscard]] auto from_payload(std::string_view payload) -> result<event> {
  return json_format<Codec>::decode(payload);
}

namespace detail {

inline constexpr std::string_view attribute_prefix = "ce-";
inline constexpr std::string_view content_type_header = "Content-Type";

struct nats_traits {
  static constexpr std::string_view attribute_prefix = ce::v3::nats::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v3::nats::detail::content_type_header;
  static constexpr bool case_sensitive_names = false;
  static constexpr bool content_type_is_attribute = true;

  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    return binding::detail::percent_encode(text);
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> result<std::string> {
    return binding::detail::percent_decode(text);
  }
};

static_assert(binding::binding_traits<nats_traits>);

}  // namespace detail

[[nodiscard]] inline auto detect_content_mode(const message& incoming) -> content_mode {
  const std::string* declared = incoming.header_fields.find(detail::content_type_header);
  if (declared != nullptr &&
      ce::v3::detail::starts_with_ignoring_case(*declared, "application/cloudevents")) {
    return content_mode::structured;
  }
  return content_mode::binary_mode;
}

template <json::json_codec Codec>
[[nodiscard]] auto to_message(const event& cloud_event, content_mode mode) -> result<message> {
  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "the NATS binding defines no batch mode");
  }

  if (mode == content_mode::structured) {
    return binding::encode_structured<detail::nats_traits, Codec>(cloud_event);
  }

  message out;
  if (auto written = binding::write_attributes<detail::nats_traits>(cloud_event, out.header_fields);
      !written) {
    return fail(written.error().code, written.error().detail, written.error().where);
  }
  binding::write_body(cloud_event, out);
  return out;
}

template <json::json_codec Codec>
[[nodiscard]] auto from_message(const message& incoming) -> result<event> {
  if (detect_content_mode(incoming) == content_mode::structured) {
    return binding::decode_structured<Codec>(incoming);
  }

  if (!incoming.header_fields.contains("ce-specversion")) {
    return fail(errc::not_a_cloudevent,
                "no ce-specversion header and no CloudEvents content type");
  }

  auto under_construction = binding::read_attributes<detail::nats_traits>(incoming.header_fields);
  if (!under_construction) {
    return fail(under_construction.error().code, under_construction.error().detail,
                under_construction.error().where);
  }

  under_construction->rest.data =
      binding::read_body(incoming.body, under_construction->rest.datacontenttype);

  return std::move(*under_construction).build();
}

}  // namespace ce::inline v3::nats
