#pragma once

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/v1/binding/common.hpp>
#include <cloudevents/v1/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/v1/format/json_format.hpp>
#include <cloudevents/v1/message.hpp>
#include <cloudevents/result.hpp>

namespace ce::v1::kafka {

namespace detail {

inline constexpr std::string_view attribute_prefix = "ce_";
inline constexpr std::string_view content_type_header = "content-type";

struct kafka_traits {
  static constexpr std::string_view attribute_prefix = ce::v1::kafka::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v1::kafka::detail::content_type_header;
  static constexpr bool case_sensitive_names = true;

  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    if (!ce::v1::detail::is_valid_utf8(text)) {
      return fail(errc::invalid_utf8, "a Kafka header value must be a UTF-8 string");
    }
    return std::string{text};
  }

  [[nodiscard]] static auto decode_value(std::string_view text) -> result<std::string> {
    return encode_value(text);
  }
};

static_assert(binding::binding_traits<kafka_traits>);

}  // namespace detail

[[nodiscard]] inline auto detect_content_mode(const message& incoming) -> content_mode {
  const std::string* declared = incoming.header_fields.find_exact(detail::content_type_header);
  if (declared == nullptr) {
    return content_mode::binary_mode;
  }
  if (std::string_view{*declared}.starts_with("application/cloudevents-batch")) {
    return content_mode::batched;
  }
  if (std::string_view{*declared}.starts_with("application/cloudevents")) {
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
    return fail(errc::invalid_argument, "the Kafka binding defines no batch mode");
  }

  if (mode == content_mode::structured) {
    return binding::encode_structured<detail::kafka_traits, Codec>(subject);
  }

  message out;
  if (auto written = binding::write_attributes<detail::kafka_traits>(subject, out.header_fields);
      !written) {
    return fail(written.error().code, written.error().detail, written.error().where);
  }
  binding::write_body(subject, out);
  return out;
}

template <json::json_codec Codec>
[[nodiscard]] auto from_message(const message& incoming) -> result<event> {
  const content_mode mode = detect_content_mode(incoming);

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "the Kafka binding defines no batch mode");
  }
  if (mode == content_mode::structured) {
    return binding::decode_structured<Codec>(incoming);
  }

  if (!incoming.header_fields.contains_exact("ce_specversion")) {
    return fail(errc::not_a_cloudevent,
                "no ce_specversion header and no CloudEvents content type");
  }

  auto subject = binding::read_attributes<detail::kafka_traits>(incoming.header_fields);
  if (!subject) {
    return fail(subject.error().code, subject.error().detail, subject.error().where);
  }

  if (const std::string* declared = incoming.header_fields.find_exact(detail::content_type_header);
      declared != nullptr) {
    subject->datacontenttype = *declared;
  }

  binding::read_body(incoming.body, *subject);

  if (auto valid = subject->validate(); !valid) {
    return fail(valid.error().code, valid.error().detail, valid.error().where);
  }

  return std::move(*subject);
}

struct record {
  message value;
  std::optional<std::string> key = {};

  friend auto operator==(const record&, const record&) -> bool = default;
};

template <class T>
concept key_mapper = requires(const event& subject) {
  { T::key_of(subject) } -> std::same_as<std::optional<std::string>>;
};

struct no_key_mapper {
  [[nodiscard]] static auto key_of(const event&) -> std::optional<std::string> {
    return std::nullopt;
  }
};

struct partitionkey_mapper {
  [[nodiscard]] static auto key_of(const event& subject) -> std::optional<std::string> {
    const attribute_value* stored = subject.extension("partitionkey");
    if (stored == nullptr) {
      return std::nullopt;
    }
    return binding::render_attribute(*stored);
  }
};

template <json::json_codec Codec, key_mapper Keys = no_key_mapper>
[[nodiscard]] auto to_record(const event& subject, content_mode mode) -> result<record> {
  auto laid_out = to_message<Codec>(subject, mode);
  if (!laid_out) {
    return fail(laid_out.error().code, laid_out.error().detail, laid_out.error().where);
  }
  return record{.value = std::move(*laid_out), .key = Keys::key_of(subject)};
}

}  // namespace ce::v1::kafka
