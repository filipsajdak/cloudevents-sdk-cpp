#pragma once

#include <cstddef>

namespace ce::v3::json {

// spec: SWR-JSON-0040
// spec: SWR-BIND-0011
// spec: SWR-BIND-0013
struct decode_options {
  static constexpr std::size_t default_retention_limit = std::size_t{16} * 1024;
  std::size_t retain_document_up_to = default_retention_limit;
  /// Whether the v4 binary-mode body reader parses a JSON body (SWR-BIND-0011).
  ///
  /// True is the default: the body is parsed, validated, and kept as a document or, above the
  /// retention limit, as text. False keeps the body's exact bytes in a `json_text`, unparsed
  /// and unvalidated, at any size, for a forwarder that never reads the payload (SWR-BIND-0012).
  /// Only that reader honours it; `ce::v3` and every other entry point accept and ignore it
  /// (SWR-BIND-0013), and it covers binary mode alone, so structured decode still parses.
  static constexpr bool parses_binary_body_by_default = true;
  bool parse_binary_body = parses_binary_body_by_default;
};

}  // namespace ce::v3::json

// spec: SWR-BUILD-0005
namespace ce::inline v4::json {
using ce::v3::json::decode_options;
}  // namespace ce::inline v4::json
