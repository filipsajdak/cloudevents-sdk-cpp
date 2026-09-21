/// \file
/// \brief Fuzz NATS payload decoding.
///
/// The decode path is the same one fuzz_json_decode drives, so this target earns
/// its place on a stronger property rather than on new coverage: decode, encode
/// and decode again must reach the same event. fuzz_json_decode only asserts that
/// re-encoding succeeds, which a format that lost an attribute would also satisfy.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <cloudevents/binding/nats.hpp>
#include <cloudevents/codec/nlohmann.hpp>

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view payload{reinterpret_cast<const char*>(data), size};

  auto decoded = ce::nats::from_payload<ce::codec::nlohmann_codec>(payload);
  if (!decoded) {
    return 0;
  }

  auto reserialized = ce::nats::to_payload<ce::codec::nlohmann_codec>(*decoded);
  if (!reserialized) {
    __builtin_trap();
  }

  auto again = ce::nats::from_payload<ce::codec::nlohmann_codec>(*reserialized);
  if (!again || !(*again == *decoded)) {
    __builtin_trap();
  }
  return 0;
}
