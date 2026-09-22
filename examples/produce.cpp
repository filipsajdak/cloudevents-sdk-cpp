/// \file
/// \brief Build an event and send it, in all three content modes.

#include <cloudevents/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/json_format.hpp>

#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

using codec = ce::codec::nlohmann_codec;

namespace {

/// Every attribute is a type that refuses what CloudEvents forbids, and a
/// literal is checked when this file compiles: `""_id` does not build. So an
/// invalid event cannot be written down, and there is nothing to validate
/// before sending one.
[[nodiscard]] auto build() -> ce::result<ce::event> {
  using namespace ce::literals;

  const auto placed_at = ce::parse_timestamp("2026-09-20T12:34:56Z");
  if (!placed_at) {
    return ce::fail(placed_at.error().code, placed_at.error().detail, placed_at.error().where);
  }

  auto order = ce::event{
      "A234-1234-1234"_id,
      "https://example.test/orders"_source,
      "com.example.order.placed"_type,
      {
          .datacontenttype = "application/json"_mediatype,
          .subject = "order-99"_subject,
          .time = *placed_at,
          .data = ce::json_text{.raw = R"({"total":42,"currency":"EUR"})"},
      },
  };

  // A typed extension writes each field under its declared attribute type, so
  // the value keeps that type on the wire wherever the format can carry it.
  // Its field names come from the struct, not from a literal, so writing one
  // can be refused and the result says so.
  if (auto traced = order.set(ce::ext::tracing{
          .traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
          .tracestate = "vendor=1",
      });
      !traced) {
    return ce::fail(traced.error().code, traced.error().detail, traced.error().where);
  }
  if (auto keyed = order.set(ce::ext::partitioning{.partitionkey = "customer-42"}); !keyed) {
    return ce::fail(keyed.error().code, keyed.error().detail, keyed.error().where);
  }

  return order;
}

void print_message(const char* title, const ce::message& request) {
  std::printf("--- %s ---\n", title);
  for (const auto& [name, value] : request.header_fields) {
    std::printf("%s: %s\n", name.c_str(), value.c_str());
  }
  std::printf("\n%s\n\n", ce::http::detail::to_text(request.body).c_str());
}

}  // namespace

int main() {
  const auto built = build();
  if (!built) {
    std::fprintf(stderr, "could not build the event at %s: %s\n", built.error().where.c_str(),
                 built.error().detail.c_str());
    return 1;
  }
  const ce::event& order = *built;

  auto structured = ce::http::to_message<codec>(order, ce::content_mode::structured);
  if (!structured) {
    std::fprintf(stderr, "structured: %s\n", structured.error().detail.c_str());
    return 1;
  }
  print_message("structured mode: the whole event in the body", *structured);

  auto binary = ce::http::to_message<codec>(order, ce::content_mode::binary_mode);
  if (!binary) {
    std::fprintf(stderr, "binary: %s\n", binary.error().detail.c_str());
    return 1;
  }
  print_message("binary mode: attributes in ce- headers, payload as the body", *binary);

  // A batch needs its own entry point, which is why asking to_message for one
  // is an invalid_argument rather than a surprise.
  const std::vector<ce::event> batch{order, order};
  auto batched = ce::http::to_batch_message<codec>(std::span<const ce::event>{batch});
  if (!batched) {
    std::fprintf(stderr, "batch: %s\n", batched.error().detail.c_str());
    return 1;
  }
  print_message("batched mode: an array of events", *batched);

  return 0;
}
