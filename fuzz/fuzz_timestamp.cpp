/// \file
/// \brief Fuzz RFC 3339 timestamp parsing.
///
/// Reachable by any peer that can set a `time` attribute. The parser must return
/// a typed error for anything it cannot represent, and never read out of bounds.

#include <cstddef>
#include <cstdint>
#include <string>

#include <cloudevents/detail/timestamp.hpp>

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
  const std::string_view text{reinterpret_cast<const char*>(data), size};
  auto parsed = ce::parse_timestamp(text);
  if (parsed) {
    // A parse that succeeded must render, and the render must parse back to the
    // same value. A round trip that diverges is a bug even when neither step
    // crashes.
    const std::string rendered = ce::to_string(*parsed);
    auto again = ce::parse_timestamp(rendered);
    if (!again || !(*again == *parsed)) {
      __builtin_trap();
    }
  }
  return 0;
}
