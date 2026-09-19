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
      if (!ce::set_data<parcel, codec>(again, *payload)) {
        __builtin_trap();
      }
      auto second = ce::data_as<parcel, codec>(again);
      if (!second || second->label != payload->label || second->weight != payload->weight ||
          second->fragile != payload->fragile || second->marks != payload->marks) {
        __builtin_trap();
      }
    }
  }

  // The payload path also has to hold up when the text is the payload itself
  // rather than a whole event.
  ce::event direct{.id = "1", .source = ce::uri_ref{"/s"}, .type = "t"};
  direct.datacontenttype = "application/json";
  direct.data = ce::json_text{.raw = std::string{text}};
  (void)ce::data_as<parcel, codec>(direct);

  return 0;
}
