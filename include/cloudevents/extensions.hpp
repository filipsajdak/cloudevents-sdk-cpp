#pragma once

/// \file
/// \brief The documented CloudEvents extensions, as described structs.

#include <cstdint>
#include <optional>
#include <string>

#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>

namespace ce::inline v1::ext {

/// \brief W3C Trace Context, carried on the event.
struct tracing {
  std::string traceparent;
  std::optional<std::string> tracestate = {};

  friend auto operator==(const tracing&, const tracing&) -> bool = default;
};

/// \brief A key grouping causally related events.
struct partitioning {
  std::string partitionkey;

  friend auto operator==(const partitioning&, const partitioning&) -> bool = default;
};

/// \brief How many similar events this one stands for. The spec requires a
/// value above zero; `validate()` on the struct is where that is checked,
/// because the CloudEvents type system cannot express it.
struct sampled_rate {
  std::int32_t sampledrate;

  friend auto operator==(const sampled_rate&, const sampled_rate&) -> bool = default;

  [[nodiscard]] auto validate() const -> result<void> {
    if (sampledrate <= 0) {
      return fail(
          errc::invalid_attribute_value, "sampledrate must be greater than zero", "sampledrate");
    }
    return {};
  }
};

/// \brief The event's relative order within its source.
///
/// The field is `value` because a member named `sequence` inside `struct
/// sequence` would hide the injected class name. The wire name is what matters
/// and it is spelled out.
struct sequence {
  std::string value;

  friend auto operator==(const sequence&, const sequence&) -> bool = default;
};

/// \brief Where the payload is stored, when it is not in the event.
struct dataref {
  uri_ref value;

  friend auto operator==(const dataref&, const dataref&) -> bool = default;
};

// CE_DESCRIBE defines a function found by ADL, so it belongs in the namespace of
// the type it describes.
CE_DESCRIBE(tracing, traceparent, tracestate);
CE_DESCRIBE(partitioning, partitionkey);
CE_DESCRIBE(sampled_rate, sampledrate);
CE_DESCRIBE(sequence, CE_FIELD(value, "sequence"));
CE_DESCRIBE(dataref, CE_FIELD(value, "dataref"));

}  // namespace ce::inline v1::ext
