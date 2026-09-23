#pragma once

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

// spec: SYS-KAFKA-0001
namespace ce::inline v3::kafka {

namespace detail {

// spec: SWR-KAFKA-0001
inline constexpr std::string_view attribute_prefix = "ce_";
// spec: SWR-KAFKA-0002
inline constexpr std::string_view content_type_header = "content-type";

struct kafka_traits {
  static constexpr std::string_view attribute_prefix = ce::v3::kafka::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v3::kafka::detail::content_type_header;
  // spec: SWR-KAFKA-0004
  static constexpr bool case_sensitive_names = true;

  // spec: SWR-KAFKA-0003
  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    if (!ce::v3::detail::is_valid_utf8(text)) {
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

// spec: SWR-KAFKA-0005
template <json::json_codec Codec>
[[nodiscard]] auto to_message(const event& cloud_event, content_mode mode) -> result<message> {
  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "the Kafka binding defines no batch mode");
  }

  if (mode == content_mode::structured) {
    return binding::encode_structured<detail::kafka_traits, Codec>(cloud_event);
  }

  message out;
  if (auto written = binding::write_attributes<detail::kafka_traits>(cloud_event, out.header_fields);
      !written) {
    return fail(written.error().code, written.error().detail, written.error().where);
  }
  binding::write_body(cloud_event, out);
  return out;
}

// spec: SWR-KAFKA-0006
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

  auto under_construction = binding::read_attributes<detail::kafka_traits>(incoming.header_fields);
  if (!under_construction) {
    return fail(under_construction.error().code, under_construction.error().detail,
                under_construction.error().where);
  }

  if (const std::string* declared = incoming.header_fields.find_exact(detail::content_type_header);
      declared != nullptr) {
    auto media_type = datacontenttype::make(*declared);
    if (!media_type) {
      return fail(media_type.error().code, media_type.error().detail, media_type.error().where);
    }
    under_construction->rest.datacontenttype = std::move(*media_type);
  }

  under_construction->rest.data =
      binding::read_body(incoming.body, under_construction->rest.datacontenttype);

  return std::move(*under_construction).build();
}

// spec: SWR-KAFKA-0007
struct record {
  message value;
  std::optional<std::string> key = {};

  friend auto operator==(const record&, const record&) -> bool = default;
};

// spec: SWR-KAFKA-0009
template <class T>
concept key_mapper = requires(const event& cloud_event) {
  { T::key_of(cloud_event) } -> std::same_as<std::optional<std::string>>;
};

struct no_key_mapper {
  [[nodiscard]] static auto key_of([[maybe_unused]] const event& cloud_event)
      -> std::optional<std::string> {
    return std::nullopt;
  }
};

struct partitionkey_mapper {
  [[nodiscard]] static auto key_of(const event& cloud_event) -> std::optional<std::string> {
    const attribute_value* stored = cloud_event.extension("partitionkey");
    if (stored == nullptr) {
      return std::nullopt;
    }
    return binding::render_attribute(*stored);
  }
};

// spec: SWR-KAFKA-0008
template <json::json_codec Codec, key_mapper Keys = no_key_mapper>
[[nodiscard]] auto to_record(const event& cloud_event, content_mode mode) -> result<record> {
  auto laid_out = to_message<Codec>(cloud_event, mode);
  if (!laid_out) {
    return fail(laid_out.error().code, laid_out.error().detail, laid_out.error().where);
  }
  return record{.value = std::move(*laid_out), .key = Keys::key_of(cloud_event)};
}

}  // namespace ce::inline v3::kafka
