// Proves the installed package does real work, not only that its headers parse.
//
// An event is written to an HTTP message in both content modes and read back, so
// the attribute checks that run on CTRE, the JSON codec and the format layer all
// execute in a binary built from nothing but the installed package. A package
// manager that supplied a CTRE or a JSON library this code cannot use fails here,
// at the point a consumer would hit it (SWR-ADOPT-0006, SWR-ADOPT-0007).

#pragma once

#include <cloudevents/binding/http.hpp>
#include <cloudevents/core.hpp>

#include <cstdio>

namespace consumer {

template <class Codec>
[[nodiscard]] auto round_trips(const ce::event& sent, ce::content_mode mode) -> bool {
  const auto message = ce::http::to_message<Codec>(sent, mode);
  if (!message) {
    std::fprintf(stderr, "write: %s\n", message.error().detail.c_str());
    return false;
  }
  const auto received = ce::http::from_message<Codec>(*message);
  if (!received) {
    std::fprintf(stderr, "read: %s\n", received.error().detail.c_str());
    return false;
  }
  // The payload is not compared: a receive parses a JSON body into a document
  // and the event written held its text, so the two differ in form, not meaning.
  return received->id() == sent.id() && received->source() == sent.source() &&
         received->type() == sent.type() && received->subject() == sent.subject() &&
         received->time() == sent.time();
}

// The process exit status: 0 when both content modes round-trip.
template <class Codec>
[[nodiscard]] auto run() -> int {
  using namespace ce::literals;

  const auto placed_at = ce::parse_timestamp("2026-09-20T12:34:56Z");
  if (!placed_at) {
    return 1;
  }
  const auto order = ce::event{
      "A234-1234-1234"_id,
      "https://example.test/orders"_source,
      "com.example.order.placed"_type,
      {
          .datacontenttype = "application/json"_mediatype,
          .subject = "order-99"_subject,
          .time = *placed_at,
          .data = ce::json_text{.raw = R"({"total":42})"},
      },
  };

  return round_trips<Codec>(order, ce::content_mode::structured) &&
                 round_trips<Codec>(order, ce::content_mode::binary_mode)
             ? 0
             : 1;
}

}  // namespace consumer
