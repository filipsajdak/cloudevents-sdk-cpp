#pragma once

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/binding/detail/protobuf_structured.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/decode_options.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

// spec: SYS-KAFKA-0001
namespace ce::inline v4::kafka {

namespace detail {

// spec: SWR-KAFKA-0001
inline constexpr std::string_view attribute_prefix = "ce_";
// spec: SWR-KAFKA-0002
inline constexpr std::string_view content_type_header = "content-type";

struct kafka_traits {
  static constexpr std::string_view attribute_prefix = ce::v4::kafka::detail::attribute_prefix;
  static constexpr std::string_view content_type_header =
      ce::v4::kafka::detail::content_type_header;
  // spec: SWR-KAFKA-0004
  static constexpr bool case_sensitive_names = true;

  // spec: SWR-KAFKA-0003
  [[nodiscard]] static auto encode_value(std::string_view text) -> result<std::string> {
    if (!ce::v4::detail::is_valid_utf8(text)) {
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
    return ce::v4::detail::forward_failure(std::move(written).error());
  }
  binding::write_body(cloud_event, out);
  return out;
}

// spec: SWR-PROTO-0022
// spec: SWR-KAFKA-0006
// spec: SWR-BIND-0009
// spec: SWR-BIND-0017
template<json::json_codec Codec>
[[nodiscard]] auto from_message(const message& incoming, json::decode_options options = {})
    -> result<event> {
  const content_mode mode = detect_content_mode(incoming);

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "the Kafka binding defines no batch mode");
  }
  if (mode == content_mode::structured) {
    return binding::decode_structured<Codec>(incoming, options);
  }

  if (!incoming.header_fields.contains_exact("ce_specversion")) {
    return fail(errc::not_a_cloudevent,
                "no ce_specversion header and no CloudEvents content type");
  }

  auto under_construction = binding::read_attributes<detail::kafka_traits>(incoming.header_fields);
  if (!under_construction) {
    return ce::v4::detail::forward_failure(std::move(under_construction).error());
  }

  if (const std::string* declared = incoming.header_fields.find_exact(detail::content_type_header);
      declared != nullptr) {
    auto media_type = datacontenttype::make(*declared);
    if (!media_type) {
      return ce::v4::detail::forward_failure(std::move(media_type).error());
    }
    under_construction->rest.datacontenttype = std::move(*media_type);
  }

  auto payload =
      binding::read_body<Codec>(incoming.body, under_construction->rest.datacontenttype, options);
  if (!payload) {
    return ce::v4::detail::forward_failure(std::move(payload).error());
  }
  under_construction->rest.data = std::move(*payload);

  return std::move(*under_construction).build();
}

// spec: SWR-KAFKA-0010
// spec: SWR-BIND-0014
// spec: SWR-BIND-0015
// spec: SWR-BIND-0016
/// `from_message` for a caller that gives the message up. A non-empty body whose media type is
/// absent or not JSON is moved into the event: after a successful call it has size 0 and capacity
/// 0, and the headers are as they were. Any other body is copied and the message is left as it
/// was, as is the message after a failed call.
template<json::json_codec Codec>
// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
[[nodiscard]] auto from_message_moving(message&& incoming, json::decode_options options = {})
    -> result<event> {
  const content_mode mode = detect_content_mode(incoming);

  if (mode == content_mode::batched) {
    return fail(errc::invalid_argument, "the Kafka binding defines no batch mode");
  }
  if (mode == content_mode::structured) {
    return binding::decode_structured<Codec>(incoming, options);
  }

  if (!incoming.header_fields.contains_exact("ce_specversion")) {
    return fail(errc::not_a_cloudevent, "no ce_specversion header and no CloudEvents content type");
  }

  auto under_construction = binding::read_attributes<detail::kafka_traits>(incoming.header_fields);
  if (!under_construction) {
    return ce::v4::detail::forward_failure(std::move(under_construction).error());
  }

  if (const std::string* declared = incoming.header_fields.find_exact(detail::content_type_header);
      declared != nullptr) {
    auto media_type = datacontenttype::make(*declared);
    if (!media_type) {
      return ce::v4::detail::forward_failure(std::move(media_type).error());
    }
    under_construction->rest.datacontenttype = std::move(*media_type);
  }

  return binding::detail::build_taking_body<Codec>(
      std::move(*under_construction), incoming.body, options);
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
    return ce::v4::detail::forward_failure(std::move(laid_out).error());
  }
  return record{.value = std::move(*laid_out), .key = Keys::key_of(cloud_event)};
}

// spec: SYS-PROTO-0002
// spec: SWR-PROTO-0020
/// The structured-mode record value of `cloud_event` in the Protobuf format. It sets no key: the
/// key is the caller's. There is no content mode: structured is the only one.
[[nodiscard]] inline auto to_message_protobuf(const event& cloud_event) -> result<message> {
  return binding::detail::encode_protobuf_structured<detail::kafka_traits>(cloud_event);
}

// spec: SWR-PROTO-0020
/// The event of a structured Protobuf record, or `errc::not_a_cloudevent` when its content-type
/// header, found under that exact name, has a media type that, compared ignoring case and
/// parameters, is not application/cloudevents+protobuf.
[[nodiscard]] inline auto from_message_protobuf(const message& incoming,
                                                protobuf::decode_options options = {})
    -> result<event> {
  return binding::detail::decode_protobuf_structured<detail::kafka_traits>(incoming, options);
}

}  // namespace ce::inline v4::kafka
