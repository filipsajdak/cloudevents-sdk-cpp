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
    ce::event out{
        .id = "A234-1234-1234",
        .source = ce::uri_ref{"https://example.test/orders/eu-west"},
        .type = "com.example.order.placed",
    };
    out.subject = "order-99";
    out.dataschema = ce::uri{"https://example.test/schema/order/v2"};
    out.datacontenttype = "application/json";
    if (auto parsed = ce::parse_timestamp("2026-09-20T12:34:56.123456789+02:00")) {
      out.time = *parsed;
    }
    (void)out.set_extension(
        "traceparent",
        ce::attribute_value{std::string{"00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01"}});
    (void)out.set_extension("partitionkey", ce::attribute_value{std::string{"customer-42"}});
    (void)out.set_extension("sampledrate", ce::attribute_value{std::int32_t{30}});
    out.data = ce::json_text{
        .raw =
            R"({"total":4299,"currency":"EUR","lines":[{"sku":"SKU-1","qty":2},{"sku":"SKU-2","qty":1}],"paid":true})"};
    return out;
  }();
  return subject;
}

/// One hundred events to encode as a batch.
[[nodiscard]] inline auto batch_events() -> const std::vector<ce::event>& {
  static const std::vector<ce::event> events = [] {
    std::vector<ce::event> out;
    out.reserve(100);
    for (int i = 0; i < 100; ++i) {
      ce::event subject{.id = "id-" + std::to_string(i),
                        .source = ce::uri_ref{"/bulk"},
                        .type = "com.example.tick"};
      subject.datacontenttype = "application/json";
      if (auto parsed = ce::parse_timestamp("2026-09-20T12:34:56Z")) {
        subject.time = *parsed;
      }
      (void)subject.set_extension("sequence",
                                  ce::attribute_value{std::to_string(1000000 + i)});
      subject.data = ce::json_text{.raw = R"({"n":)" + std::to_string(i) + "}"};
      out.push_back(std::move(subject));
    }
    return out;
  }();
  return events;
}

}  // namespace ce::bench
