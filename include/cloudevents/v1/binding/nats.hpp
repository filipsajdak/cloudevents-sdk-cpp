#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/v1/binding/common.hpp>
#include <cloudevents/v1/binding/detail/percent.hpp>
#include <cloudevents/v1/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/v1/format/json_format.hpp>
#include <cloudevents/v1/message.hpp>
#include <cloudevents/result.hpp>

namespace ce::v1::nats {

template <json::json_codec Codec>
[[nodiscard]] auto to_payload(const event& subject) -> result<std::string> {
  return json_format<Codec>::encode(subject);
}

template <json::json_codec Codec>
[[nodiscard]] auto from_payload(std::string_view payload) -> result<event> {
  return json_format<Codec>::decode(payload);
}

namespace detail {

inline constexpr std::string_view attribute_prefix = "ce-";
inline constexpr std::string_view content_type_header = "Content-Type";

struct nats_traits {
  static constexpr std::string_view attribute_prefix = ce::v1::nats::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v1::nats::detail::content_type_header;
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
      ce::v1::detail::starts_with_ignoring_case(*declared, "application/cloudevents")) {
    return content_mode::structured;
  }
  return content_mode::binary_mode;
}

template <json::json_codec Codec>
[[nodiscard]] auto to_message(const event& subject, content_mode mode) -> result<message> {
  if (auto valid = subject.validate(); !valid) {
    return fail(valid.error().code, valid.error().detail, valid.error().where);
  }

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "the NATS binding defines no batch mode");
  }

  if (mode == content_mode::structured) {
    return binding::encode_structured<detail::nats_traits, Codec>(subject);
  }

  message out;
  if (auto written = binding::write_attributes<detail::nats_traits>(subject, out.header_fields);
      !written) {
    return fail(written.error().code, written.error().detail, written.error().where);
  }
  binding::write_body(subject, out);
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

  auto subject = binding::read_attributes<detail::nats_traits>(incoming.header_fields);
  if (!subject) {
    return fail(subject.error().code, subject.error().detail, subject.error().where);
  }

  binding::read_body(incoming.body, *subject);

  if (auto valid = subject->validate(); !valid) {
    return fail(valid.error().code, valid.error().detail, valid.error().where);
  }
  return std::move(*subject);
}

}  // namespace ce::v1::nats
