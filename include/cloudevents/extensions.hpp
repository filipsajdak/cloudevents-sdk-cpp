#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>

// spec: SWR-EXT-0001
namespace ce::inline v1::ext {

struct tracing {
  std::string traceparent;
  std::optional<std::string> tracestate = {};

  friend auto operator==(const tracing&, const tracing&) -> bool = default;
};

struct partitioning {
  std::string partitionkey;

  friend auto operator==(const partitioning&, const partitioning&) -> bool = default;
};

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

struct sequence {
  std::string value;

  friend auto operator==(const sequence&, const sequence&) -> bool = default;
};

struct dataref {
  uri_ref value;

  friend auto operator==(const dataref&, const dataref&) -> bool = default;
};

CE_DESCRIBE(tracing, traceparent, tracestate);
CE_DESCRIBE(partitioning, partitionkey);
CE_DESCRIBE(sampled_rate, sampledrate);
CE_DESCRIBE(sequence, CE_FIELD(value, "sequence"));
CE_DESCRIBE(dataref, CE_FIELD(value, "dataref"));

}  // namespace ce::inline v1::ext
