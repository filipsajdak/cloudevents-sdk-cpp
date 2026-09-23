/// \file
/// \brief Fuzz JSON event decoding, through both codecs.
///
/// This is the structured-mode receive path: a whole event document straight off
/// the wire.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/v2/format/json_format.hpp>

#include "test/v2/mini_codec.hpp"

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view text{reinterpret_cast<const char*>(data), size};

  using format = ce::v2::json_format<ce::v2::codec::nlohmann_codec>;
  auto decoded = format::decode(text);
  if (decoded) {
    // An event that decoded must re-encode, and the encoding must decode to the
    // same event: the decoder must not produce a value the encoder rejects or
    // rewrites (SWR-JSON-0031).
    auto encoded = format::encode(*decoded);
    if (!encoded) {
      __builtin_trap();
    }
    auto again = format::decode(*encoded);
    if (!again || !(*again == *decoded)) {
      __builtin_trap();
    }
  }

  // The second codec is not redundant: the two parse independently, so an input
  // one accepts and the other rejects is a disagreement worth finding.
  (void)ce::v2::json_format<ce::test::mini_codec>::decode(text);
  (void)ce::v2::json_format<ce::v2::codec::nlohmann_codec>::decode_batch(text);
  return 0;
}
