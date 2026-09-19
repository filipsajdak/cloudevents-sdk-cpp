#pragma once

/// \file
/// \brief The result of compiling the SDK's public headers with no nlohmann.
///
/// Deliberately free of `// spec:` markers: the markers live in
/// json_codec_test.cpp, which is the file the requirements name. This header is
/// also deliberately free of any nlohmann include, because the translation unit
/// that implements it is the evidence for SWR-JSON-0008.

#include <string>

namespace ce_no_nlohmann {

/// \brief What the nlohmann-free translation unit observed about itself.
struct report {
  /// True when nlohmann's own version macro is defined, which it is only after
  /// nlohmann/json.hpp has been included somewhere in the translation unit.
  bool nlohmann_macro_defined;
  /// True when json_format over a user-supplied codec encoded and decoded an
  /// event in that same translation unit.
  bool format_round_tripped;
  /// The document json_format produced there, so the caller can inspect it.
  std::string encoded;
  /// True when a described payload went out and came back through the typed
  /// payload accessors in that same translation unit, which is what shows they
  /// name no codec of their own (SWR-EXT-0006).
  bool typed_payload_round_tripped;
  /// True when a typed extension struct read back what it wrote there, which
  /// shows the typed extension layer is core and pulls in no codec.
  bool typed_extension_round_tripped;
};

/// \brief Run the probe. Its value is what its translation unit proves, not what
/// it returns: it is compiled from the SDK's public headers with the nlohmann
/// codec header excluded.
[[nodiscard]] auto probe() -> report;

}  // namespace ce_no_nlohmann
