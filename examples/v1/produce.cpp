/// \file
/// \brief Build an event and send it, in all three content modes.

#include <cloudevents/v1/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/v1/core.hpp>
#include <cloudevents/v1/extensions.hpp>
#include <cloudevents/v1/format/json_format.hpp>

#include <cstdio>
#include <span>
#include <string>
#include <vector>

using codec = ce::v1::codec::nlohmann_codec;

namespace {

/// An event is a public aggregate. The required attributes come first and have
/// no defaults, so leaving one out is a compile error rather than an invalid
/// event discovered later.
[[nodiscard]] auto build() -> ce::v1::event {
  ce::v1::event order{
      .id = "A234-1234-1234",
      .source = ce::v1::uri_ref{"https://example.test/orders"},
      .type = "com.example.order.placed",
  };
  order.subject = "order-99";
  order.datacontenttype = "application/json";
  order.data = ce::v1::json_text{.raw = R"({"total":42,"currency":"EUR"})"};

  if (auto now = ce::v1::parse_timestamp("2026-09-20T12:34:56Z")) {
    order.time = *now;
  }

  // A typed extension writes each field under its declared attribute type, so
  // the value keeps that type on the wire wherever the format can carry it.
  (void)order.set(ce::v1::ext::tracing{
      .traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
      .tracestate = "vendor=1",
  });
  (void)order.set(ce::v1::ext::partitioning{.partitionkey = "customer-42"});

  return order;
}

void print_message(const char* title, const ce::v1::message& request) {
  std::printf("--- %s ---\n", title);
  for (const auto& [name, value] : request.header_fields) {
    std::printf("%s: %s\n", name.c_str(), value.c_str());
  }
  std::printf("\n%s\n\n", ce::v1::http::detail::to_text(request.body).c_str());
}

}  // namespace

int main() {
  const ce::v1::event order = build();

  // Nothing is sent until the event is valid. validate() is the gate.
  if (auto valid = order.validate(); !valid) {
    std::fprintf(stderr, "invalid event at %s: %s\n", valid.error().where.c_str(),
                 valid.error().detail.c_str());
    return 1;
  }

  auto structured = ce::v1::http::to_message<codec>(order, ce::v1::content_mode::structured);
  if (!structured) {
    std::fprintf(stderr, "structured: %s\n", structured.error().detail.c_str());
    return 1;
  }
  print_message("structured mode: the whole event in the body", *structured);

  auto binary = ce::v1::http::to_message<codec>(order, ce::v1::content_mode::binary_mode);
  if (!binary) {
    std::fprintf(stderr, "binary: %s\n", binary.error().detail.c_str());
    return 1;
  }
  print_message("binary mode: attributes in ce- headers, payload as the body", *binary);

  // A batch needs its own entry point, which is why asking to_message for one
  // is an invalid_argument rather than a surprise.
  const std::vector<ce::v1::event> batch{order, order};
  auto batched = ce::v1::http::to_batch_message<codec>(std::span<const ce::v1::event>{batch});
  if (!batched) {
    std::fprintf(stderr, "batch: %s\n", batched.error().detail.c_str());
    return 1;
  }
  print_message("batched mode: an array of events", *batched);

  return 0;
}
