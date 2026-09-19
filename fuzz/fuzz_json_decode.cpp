/// \file
/// \brief Fuzz JSON event decoding, through both codecs.
///
/// This is the structured-mode receive path: a whole event document straight off
/// the wire.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/format/json_format.hpp>

#include "../test/mini_codec.hpp"

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view text{reinterpret_cast<const char*>(data), size};

  auto decoded = ce::json_format<ce::codec::nlohmann_codec>::decode(text);
  if (decoded) {
    // An event that decoded must validate and re-encode: the decoder must not
    // produce a value the encoder then rejects.
    if (!decoded->validate()) {
      __builtin_trap();
    }
    if (!ce::json_format<ce::codec::nlohmann_codec>::encode(*decoded)) {
      __builtin_trap();
    }
  }

  // The second codec is not redundant: the two parse independently, so an input
  // one accepts and the other rejects is a disagreement worth finding.
  (void)ce::json_format<ce::test::mini_codec>::decode(text);
  (void)ce::json_format<ce::codec::nlohmann_codec>::decode_batch(text);
  return 0;
}
