/// \file
/// \brief Build an event and send it, in all three content modes.

#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/v2/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/v2/format/json_format.hpp>

#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

using codec = ce::v2::codec::nlohmann_codec;

namespace {

/// Every attribute is a type that refuses what CloudEvents forbids, and a
/// literal is checked when this file compiles: `""_id` does not build. So an
/// invalid event cannot be written down, and there is nothing to validate
/// before sending one.
[[nodiscard]] auto build() -> ce::v2::result<ce::v2::event> {
  using namespace ce::v2::literals;

  const auto placed_at = ce::v2::parse_timestamp("2026-09-20T12:34:56Z");
  if (!placed_at) {
    return ce::v2::fail(placed_at.error().code, placed_at.error().detail, placed_at.error().where);
  }

  auto order = ce::v2::event{
      "A234-1234-1234"_id,
      "https://example.test/orders"_source,
      "com.example.order.placed"_type,
      {
          .datacontenttype = "application/json"_mediatype,
          .subject = "order-99"_subject,
          .time = *placed_at,
          .data = ce::v2::json_text{.raw = R"({"total":42,"currency":"EUR"})"},
      },
  };

  // A typed extension writes each field under its declared attribute type, so
  // the value keeps that type on the wire wherever the format can carry it.
  // Its field names come from the struct, not from a literal, so writing one
  // can be refused and the result says so.
  if (auto traced = order.set(ce::v2::ext::tracing{
          .traceparent = "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01",
          .tracestate = "vendor=1",
      });
      !traced) {
    return ce::v2::fail(traced.error().code, traced.error().detail, traced.error().where);
  }
  if (auto keyed = order.set(ce::v2::ext::partitioning{.partitionkey = "customer-42"}); !keyed) {
    return ce::v2::fail(keyed.error().code, keyed.error().detail, keyed.error().where);
  }

  return order;
}

void print_message(const char* title, const ce::v2::message& request) {
  std::printf("--- %s ---\n", title);
  for (const auto& [name, value] : request.header_fields) {
    std::printf("%s: %s\n", name.c_str(), value.c_str());
  }
  std::printf("\n%s\n\n", ce::v2::http::detail::to_text(request.body).c_str());
}

}  // namespace

int main() {
  const auto built = build();
  if (!built) {
    std::fprintf(stderr, "could not build the event at %s: %s\n", built.error().where.c_str(),
                 built.error().detail.c_str());
    return 1;
  }
  const ce::v2::event& order = *built;

  auto structured = ce::v2::http::to_message<codec>(order, ce::v2::content_mode::structured);
  if (!structured) {
    std::fprintf(stderr, "structured: %s\n", structured.error().detail.c_str());
    return 1;
  }
  print_message("structured mode: the whole event in the body", *structured);

  auto binary = ce::v2::http::to_message<codec>(order, ce::v2::content_mode::binary_mode);
  if (!binary) {
    std::fprintf(stderr, "binary: %s\n", binary.error().detail.c_str());
    return 1;
  }
  print_message("binary mode: attributes in ce- headers, payload as the body", *binary);

  // A batch needs its own entry point, which is why asking to_message for one
  // is an invalid_argument rather than a surprise.
  const std::vector<ce::v2::event> batch{order, order};
  auto batched = ce::v2::http::to_batch_message<codec>(std::span<const ce::v2::event>{batch});
  if (!batched) {
    std::fprintf(stderr, "batch: %s\n", batched.error().detail.c_str());
    return 1;
  }
  print_message("batched mode: an array of events", *batched);

  return 0;
}
