#pragma once

/// \file
/// \brief The documents the benchmark measures, and the events behind them.
///
/// Embedded rather than read from `test/fixtures/`, so a run is reproducible
/// from the binary alone and the sizes are stated rather than incidental.
/// Every one is a real CloudEvent: these are the shapes a service actually
/// receives, not synthetic JSON chosen to flatter a parser.

#include <cloudevents/core.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ce::bench {

using namespace std::string_view_literals;

/// The four required attributes and nothing else. 96 bytes.
inline constexpr auto minimal_document =
    R"({"specversion":"1.0","id":"A234-1234-1234","source":"/mycontext","type":"com.example.someevent"})"sv;

/// Every optional context attribute, three extensions, and a JSON payload.
/// The shape a production event usually has. 414 bytes.
inline constexpr auto full_document =
    R"({"specversion":"1.0","id":"A234-1234-1234","source":"https://example.test/orders/eu-west","type":"com.example.order.placed","subject":"order-99","time":"2026-09-20T12:34:56.123456789+02:00","datacontenttype":"application/json","dataschema":"https://example.test/schema/order/v2","traceparent":"00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01","partitionkey":"customer-42","sampledrate":30,"data":{"total":4299,"currency":"EUR","lines":[{"sku":"SKU-1","qty":2},{"sku":"SKU-2","qty":1}],"paid":true}})"sv;

/// A payload big enough that parsing dominates the attribute handling.
/// Built once at startup rather than embedded, to keep this header readable.
[[nodiscard]] inline auto large_document() -> const std::string& {
  static const std::string document = [] {
    std::string lines;
    lines.reserve(64 * 1024);
    lines += R"({"specversion":"1.0","id":"A234-1234-1234",)";
    lines += R"("source":"https://example.test/orders","type":"com.example.report",)";
    lines += R"("datacontenttype":"application/json","data":{"rows":[)";
    for (int i = 0; i < 500; ++i) {
      if (i != 0) {
        lines += ',';
      }
      lines += R"({"sku":"SKU-)";
      lines += std::to_string(i);
      lines += R"(","qty":)";
      lines += std::to_string(i % 17);
      lines += R"(,"price":)";
      lines += std::to_string(i * 3 + 1);
      lines += R"(,"note":"a line of text that is long enough to matter when parsing"})";
    }
    lines += "]}}";
    return lines;
  }();
  return document;
}

/// One hundred events in a batch, the shape a bulk ingest endpoint receives.
[[nodiscard]] inline auto batch_document() -> const std::string& {
  static const std::string document = [] {
    std::string out = "[";
    for (int i = 0; i < 100; ++i) {
      if (i != 0) {
        out += ',';
      }
      out += R"({"specversion":"1.0","id":"id-)";
      out += std::to_string(i);
      out += R"(","source":"/bulk","type":"com.example.tick","time":"2026-09-20T12:34:56Z",)";
      out += R"("sequence":")";
      out += std::to_string(1000000 + i);
      out += R"(","datacontenttype":"application/json","data":{"n":)";
      out += std::to_string(i);
      out += "}}";
    }
    out += "]";
    return out;
  }();
  return document;
}

/// The event the encode benchmarks start from: the decoded `full_document`.
[[nodiscard]] inline auto full_event() -> const ce::event& {
  static const ce::event subject = [] {
    using namespace ce::literals;
    const auto parsed = ce::parse_timestamp("2026-09-20T12:34:56.123456789+02:00");
    return ce::event{
        "A234-1234-1234"_id,
        "https://example.test/orders/eu-west"_source,
        "com.example.order.placed"_type,
        {
            .datacontenttype = "application/json"_mediatype,
            .dataschema = "https://example.test/schema/order/v2"_dataschema,
            .subject = "order-99"_subject,
            .time = parsed ? std::optional<ce::timestamp>{*parsed} : std::nullopt,
            .extensions =
                {
                    {"traceparent"_ext,
                     std::string{"00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}},
                    {"partitionkey"_ext, std::string{"customer-42"}},
                    {"sampledrate"_ext, std::int32_t{30}},
                },
            .data = ce::json_text{
                .raw =
                    R"({"total":4299,"currency":"EUR","lines":[{"sku":"SKU-1","qty":2},{"sku":"SKU-2","qty":1}],"paid":true})"},
        },
    };
  }();
  return subject;
}

/// One hundred events to encode as a batch.
[[nodiscard]] inline auto batch_events() -> const std::vector<ce::event>& {
  static const std::vector<ce::event> events = [] {
    using namespace ce::literals;
    const auto parsed = ce::parse_timestamp("2026-09-20T12:34:56Z");
    std::vector<ce::event> out;
    out.reserve(100);
    for (int i = 0; i < 100; ++i) {
      // The id is made at run time, so it goes through the factory; "id-N" always
      // passes, and a refusal would only shrink the batch, never corrupt it.
      auto id = ce::id::make("id-" + std::to_string(i));
      if (!id) {
        continue;
      }
      out.emplace_back(std::move(*id), "/bulk"_source, "com.example.tick"_type,
                       ce::event::options{
                           .datacontenttype = "application/json"_mediatype,
                           .time = parsed ? std::optional<ce::timestamp>{*parsed} : std::nullopt,
                           .extensions = {{"sequence"_ext, std::to_string(1000000 + i)}},
                           .data = ce::json_text{.raw = R"({"n":)" + std::to_string(i) + "}"},
                       });
    }
    return out;
  }();
  return events;
}

}  // namespace ce::bench
