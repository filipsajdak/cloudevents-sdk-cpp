#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

namespace ce::inline v4::binding::detail {

inline constexpr std::string_view media_type_parameter_separator = ";";
inline constexpr std::string_view media_type_padding = " \t";

/// The media type of a Content-Type value: what precedes the first `;`, less the blanks around it.
[[nodiscard]] constexpr auto media_type_essence(std::string_view declared) noexcept
    -> std::string_view {
  constexpr auto none = std::string_view::npos;
  std::string_view bare = declared;
  bare.remove_suffix(bare.size() -
                     std::min(bare.find(media_type_parameter_separator), bare.size()));
  bare.remove_prefix(std::min(bare.find_first_not_of(media_type_padding), bare.size()));
  const auto last = bare.find_last_not_of(media_type_padding);
  bare.remove_suffix(bare.size() - (last == none ? 0 : last + 1));
  return bare;
}

// spec: SWR-PROTO-0019
// spec: SWR-PROTO-0020
/// The structured-mode message of a binding in the Protobuf format: its content type, written
/// under the binding's own header spelling, and the bytes of `protobuf_format::encode`.
template<binding_traits T>
[[nodiscard]] auto encode_protobuf_structured(const event& cloud_event) -> result<message> {
  auto body = protobuf_format::encode(cloud_event);
  if (!body) {
    return ce::v4::detail::forward_failure(std::move(body).error());
  }
  return message{
      .header_fields = {{std::string{T::content_type_header}, std::string{protobuf::content_type}}},
      .body = std::move(*body),
  };
}

// spec: SWR-PROTO-0019
// spec: SWR-PROTO-0020
/// The event of a structured Protobuf message. The content type is looked up under the binding's
/// own rule for header names, and its media type is compared whole, ignoring case and parameters.
template<binding_traits T>
[[nodiscard]] auto decode_protobuf_structured(const message& from,
                                              const protobuf::decode_options& options)
    -> result<event> {
  const std::string* declared = nullptr;
  if constexpr (T::case_sensitive_names) {
    declared = from.header_fields.find_exact(T::content_type_header);
  } else {
    declared = from.header_fields.find(T::content_type_header);
  }
  if (declared == nullptr ||
      !ce::v4::detail::iequals(media_type_essence(*declared), protobuf::content_type)) {
    return fail(errc::not_a_cloudevent, "the content type is not application/cloudevents+protobuf");
  }
  return protobuf_format::decode(from.body, options);
}

}  // namespace ce::inline v4::binding::detail
