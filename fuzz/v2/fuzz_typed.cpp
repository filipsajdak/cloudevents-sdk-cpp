/// \file
/// \brief Fuzz the typed layer: extension type recovery and typed payloads.
///
/// Both read untrusted text. get<Ext>() parses an attribute string back into a
/// declared type, and data_as<T>() parses a payload document into a struct.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/v2/format/json_format.hpp>
#include <cloudevents/v2/format/typed_payload.hpp>

namespace {

using codec = ce::v2::codec::nlohmann_codec;

struct parcel {
  std::string label;
  std::int32_t weight;
  bool fragile;
  std::vector<std::string> marks;
};

CE_DESCRIBE(parcel, label, weight, fragile, marks);

template <class Ext>
void check_extension_roundtrip(const ce::v2::event& subject) {
  auto read = subject.get<Ext>();
  if (!read) {
    return;
  }
  // What was recovered must survive being written back and read again: the
  // declared type is meant to be stable once it has been restored.
  ce::v2::event again = subject;
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

  auto decoded = ce::v2::json_format<codec>::decode(text);
  if (decoded) {
    check_extension_roundtrip<ce::v2::ext::tracing>(*decoded);
    check_extension_roundtrip<ce::v2::ext::partitioning>(*decoded);
    check_extension_roundtrip<ce::v2::ext::sampled_rate>(*decoded);
    check_extension_roundtrip<ce::v2::ext::sequence>(*decoded);
    check_extension_roundtrip<ce::v2::ext::dataref>(*decoded);

    // A payload that read back must survive being written and read again.
    if (auto payload = ce::v2::data_as<parcel, codec>(*decoded)) {
      ce::v2::event again = *decoded;
      ce::v2::set_data<parcel, codec>(again, *payload);
      auto second = ce::v2::data_as<parcel, codec>(again);
      if (!second || second->label != payload->label || second->weight != payload->weight ||
          second->fragile != payload->fragile || second->marks != payload->marks) {
        __builtin_trap();
      }
    }
  }

  // The payload path also has to hold up when the text is the payload itself
  // rather than a whole event.
  using namespace ce::v2::literals;
  const ce::v2::event direct{"1"_id, "/s"_source, "t"_type,
                         {.datacontenttype = "application/json"_mediatype,
                          .data = ce::v2::json_text{.raw = std::string{text}}}};
  (void)ce::v2::data_as<parcel, codec>(direct);

  return 0;
}
