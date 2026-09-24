#pragma once

/// \file
/// \brief Compare a JSON payload by value, however an event holds it.
///
/// A decoded payload may be `json_text` or `json_document` depending on the path
/// it took, and text differs in spelling between codecs. Parsing both sides with
/// one codec and comparing with its `equal` asserts what a round trip promises.

#include <string_view>
#include <variant>

#include <cloudevents/core.hpp>

namespace ce_test {

template <class Codec>
[[nodiscard]] auto same_json_payload(const ce::data_t& data, std::string_view expected) -> bool {
  const auto wanted = Codec::parse(expected);
  if (!wanted) {
    return false;
  }
  if (const auto* text = std::get_if<ce::json_text>(&data)) {
    const auto held = Codec::parse(text->raw);
    return held.has_value() && Codec::equal(*held, *wanted);
  }
  if (const auto* document = std::get_if<ce::json_document>(&data)) {
    const auto held = Codec::parse(document->dump());
    return held.has_value() && Codec::equal(*held, *wanted);
  }
  return false;
}

}  // namespace ce_test
