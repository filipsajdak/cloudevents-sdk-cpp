#pragma once

#include <cstddef>

namespace ce::v3::json {

// spec: SWR-JSON-0040
struct decode_options {
  static constexpr std::size_t default_retention_limit = std::size_t{16} * 1024;
  std::size_t retain_document_up_to = default_retention_limit;
};

}  // namespace ce::v3::json

// spec: SWR-BUILD-0005
namespace ce::inline v4::json {
using ce::v3::json::decode_options;
}  // namespace ce::inline v4::json
