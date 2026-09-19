/// \file
/// \brief Fuzz base64 decoding.
///
/// Reachable through `data_base64` and through any binary attribute.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <cloudevents/format/base64.hpp>

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view text{reinterpret_cast<const char*>(data), size};
  auto decoded = ce::base64_decode(text);
  if (decoded) {
    // Anything that decoded must re-encode and decode back to the same bytes.
    // Otherwise two spellings decode alike and peers disagree about equality.
    auto again = ce::base64_decode(ce::base64_encode(*decoded));
    if (!again || *again != *decoded) {
      __builtin_trap();
    }
  }
  return 0;
}
