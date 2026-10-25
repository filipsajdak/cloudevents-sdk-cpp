/// \file
/// \brief Fuzz the Protobuf event decoder.
///
/// A hand-written parser of untrusted bytes: lengths, varints, a map, an opaque Any
/// (SWR-PROTO-0017, SWR-SEC-0001). Whatever it accepts must re-encode, and the encoding must
/// decode to the same event (SWR-PROTO-0012).

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <cloudevents/format/protobuf_format.hpp>

namespace {

using format = ce::protobuf_format;

/// Limits tight enough that a short input reaches every limit check.
constexpr ce::protobuf::decode_options tight{
    .max_encoded_size = 64,
    .max_attribute_count = 2,
    .max_batch_event_count = 2,
};

/// An event that decoded must re-encode, deterministically, and decode again to an equal event.
void check_event_round_trip(const ce::event& decoded) {
  const auto encoded = format::encode(decoded);
  if (!encoded) {
    __builtin_trap();
  }
  const auto again = format::decode(*encoded);
  if (!again || !(*again == decoded)) {
    __builtin_trap();
  }
  const auto repeated = format::encode(decoded);
  if (!repeated || *repeated != *encoded) {
    __builtin_trap();
  }
}

void check_batch_round_trip(std::span<const ce::event> events) {
  const auto encoded = format::encode_batch(events);
  if (!encoded) {
    __builtin_trap();
  }
  const auto again = format::decode_batch(*encoded);
  if (!again || !(*again == std::vector<ce::event>{events.begin(), events.end()})) {
    __builtin_trap();
  }
}

/// A decode under tight limits either fails or gives what the default limits give.
void check_limits_agree(std::span<const std::byte> input, const ce::result<ce::event>& unlimited) {
  const auto limited = format::decode(input, tight);
  if (limited && (!unlimited || !(*limited == *unlimited))) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::span<const std::byte> input{reinterpret_cast<const std::byte*>(data), size};

  const auto decoded = format::decode(input);
  if (decoded) {
    check_event_round_trip(*decoded);
  }
  check_limits_agree(input, decoded);

  // The side channel reads the same bytes, so it agrees with the plain decode.
  const auto detailed = format::decode_detailed(input);
  if (detailed.has_value() != decoded.has_value() || (detailed && !(detailed->event == *decoded))) {
    __builtin_trap();
  }

  const auto batch = format::decode_batch(input);
  if (batch) {
    check_batch_round_trip(*batch);
  }
  (void)format::decode_batch(input, tight);
  return 0;
}
