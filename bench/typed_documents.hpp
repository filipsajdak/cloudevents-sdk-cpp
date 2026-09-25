#pragma once

/// \file
/// \brief The described payload types the typed benchmarks read and write,
/// and the events that carry them.
///
/// Shared by `codec_bench.cpp` and `perf/perf_probe.cpp`, so a wall-time
/// number and a count describe the same work. Each type is declared in
/// `ce::bench`, which is where `CE_DESCRIBE`'s function is found by ADL.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/typed_payload.hpp>

namespace ce::bench {

/// The payload the typed read and write measure on their own.
struct order {
  std::int32_t total;
  std::string currency;
  bool paid;
  std::vector<std::string> tags;
};

CE_DESCRIBE(order, total, currency, paid, tags);

/// The scalar members of `full_document`'s payload. Its `lines` hold objects
/// of mixed member types, which the describe seam has no field type for, so
/// they are left unread as any absent field would be.
struct placed {
  std::int32_t total;
  std::string currency;
  bool paid;
};

CE_DESCRIBE(placed, total, currency, paid);

/// `large_document`'s payload holds only `rows` of mixed member types, so the
/// typed read of it measures the event and the payload's retention, not the
/// payload's fields.
struct report {
  std::optional<std::string> title;
};

CE_DESCRIBE(report, title);

/// The payload of each event in `batch_document`.
struct tick {
  std::int32_t n;
};

CE_DESCRIBE(tick, n);

[[nodiscard]] inline auto typed_order() -> const order& {
  static const order payload{
      .total = 4299, .currency = "EUR", .paid = true, .tags = {"eu", "priority"}};
  return payload;
}

[[nodiscard]] inline auto typed_placed() -> const placed& {
  static const placed payload{.total = 4299, .currency = "EUR", .paid = true};
  return payload;
}

/// An event whose payload `set_data` wrote with the codec that reads it.
template<class C>
[[nodiscard]] auto typed_event() -> const ce::event& {
  static const ce::event subject = [] {
    using namespace ce::literals;
    ce::event out{"A234"_id, "/orders"_source, "com.example.order"_type};
    ce::set_data<order, C>(out, typed_order());
    return out;
  }();
  return subject;
}

/// An event holding the payload as a document the reading codec built.
template<class C>
[[nodiscard]] auto typed_document_event() -> const ce::event& {
  static const ce::event subject = [] {
    using namespace ce::literals;
    return ce::event{"A234"_id,
                     "/orders"_source,
                     "com.example.order"_type,
                     {.datacontenttype = "application/json"_mediatype,
                      .data = ce::json_document::make<C>(ce::to_json_value<C>(typed_order()))}};
  }();
  return subject;
}

}  // namespace ce::bench
