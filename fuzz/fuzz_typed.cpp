/// \file
/// \brief Fuzz the typed layer: extension type recovery and typed payloads.
///
/// Both read untrusted text. get<Ext>() parses an attribute string back into a
/// declared type, and data_as<T>() parses a payload document into a struct.
/// decode_as<T>() does both in one parse, and must agree with decode followed
/// by data_as, within the retention limit and above it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>

namespace {

using codec = ce::codec::nlohmann_codec;

struct parcel {
  std::string label;
  std::int32_t weight;
  bool fragile;
  std::vector<std::string> marks;
};

CE_DESCRIBE(parcel, label, weight, fragile, marks);

[[nodiscard]] auto same_parcel(const parcel& left, const parcel& right) -> bool {
  return left.label == right.label && left.weight == right.weight &&
         left.fragile == right.fragile && left.marks == right.marks;
}

void check_decode_as_agrees(std::string_view text, ce::json::decode_options options) {
  const auto typed = ce::decode_as<parcel, codec>(text, options);
  const auto event = ce::json_format<codec>::decode(text, options);
  if (!event) {
    if (typed) {
      __builtin_trap();
    }
    return;
  }
  const auto payload = ce::data_as<parcel, codec>(*event);
  if (typed.has_value() != payload.has_value()) {
    __builtin_trap();
  }
  if (!typed) {
    if (typed.error().code != payload.error().code) {
      __builtin_trap();
    }
    return;
  }
  if (!(typed->event == *event) || !same_parcel(typed->payload, *payload)) {
    __builtin_trap();
  }
}

template <class Ext>
void check_extension_roundtrip(const ce::event& subject) {
  auto read = subject.get<Ext>();
  if (!read) {
    return;
  }
  // What was recovered must survive being written back and read again: the
  // declared type is meant to be stable once it has been restored.
  ce::event again = subject;
  if (!again.set(*read)) {
    __builtin_trap();
  }
  auto second = again.get<Ext>();
  if (!second || !(*second == *read)) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view text{reinterpret_cast<const char*>(data), size};

  check_decode_as_agrees(text, {});
  check_decode_as_agrees(text, {.retain_document_up_to = 0});

  auto decoded = ce::json_format<codec>::decode(text);
  if (decoded) {
    check_extension_roundtrip<ce::ext::tracing>(*decoded);
    check_extension_roundtrip<ce::ext::partitioning>(*decoded);
    check_extension_roundtrip<ce::ext::sampled_rate>(*decoded);
    check_extension_roundtrip<ce::ext::sequence>(*decoded);
    check_extension_roundtrip<ce::ext::dataref>(*decoded);

    // A payload that read back must survive being written and read again.
    if (auto payload = ce::data_as<parcel, codec>(*decoded)) {
      ce::event again = *decoded;
      ce::set_data<parcel, codec>(again, *payload);
      auto second = ce::data_as<parcel, codec>(again);
      if (!second || !same_parcel(*second, *payload)) {
        __builtin_trap();
      }
      // encode_as writes what set_data then encode would, and reads back.
      auto written = ce::encode_as<parcel, codec>(*decoded, *payload);
      const auto media_type = decoded->datacontenttype();
      const bool refusable = media_type && !ce::is_json_content_type(media_type->view());
      if (written.has_value() == refusable) {
        __builtin_trap();
      }
      if (written) {
        auto back = ce::decode_as<parcel, codec>(*written);
        if (!back || !same_parcel(back->payload, *payload)) {
          __builtin_trap();
        }
      }
    }
  }

  // The payload path also has to hold up when the text is the payload itself
  // rather than a whole event.
  using namespace ce::literals;
  const ce::event direct{"1"_id, "/s"_source, "t"_type,
                         {.datacontenttype = "application/json"_mediatype,
                          .data = ce::json_text{.raw = std::string{text}}}};
  (void)ce::data_as<parcel, codec>(direct);

  return 0;
}
