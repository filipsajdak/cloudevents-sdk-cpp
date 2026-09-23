/// \file
/// \brief Receive an HTTP message without knowing which mode it arrived in.

#include <cloudevents/v1/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/v1/core.hpp>
#include <cloudevents/v1/extensions.hpp>

#include <cstdio>
#include <string>
#include <variant>
#include <vector>

using codec = ce::v1::codec::nlohmann_codec;

namespace {

void describe(const ce::v1::event& subject) {
  std::printf("  id=%s type=%s source=%s\n", subject.id.c_str(), subject.type.c_str(),
              std::string{subject.source.view()}.c_str());

  if (subject.time) {
    std::printf("  time=%s\n", ce::v1::to_string(*subject.time).c_str());
  }

  // The payload is one of four things, and the variant says which rather than
  // leaving the caller to guess from datacontenttype.
  std::visit(
      [](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          std::printf("  data: (none)\n");
        } else if constexpr (std::is_same_v<T, std::string>) {
          std::printf("  data: text, %zu bytes\n", payload.size());
        } else if constexpr (std::is_same_v<T, ce::v1::binary>) {
          std::printf("  data: %zu bytes of binary\n", payload.size());
        } else {
          std::printf("  data: JSON %s\n", payload.raw.c_str());
        }
      },
      subject.data);

  // A typed extension restores the declared type even when the wire form lost
  // it, which binary mode always does.
  if (auto tracing = subject.get<ce::v1::ext::tracing>()) {
    std::printf("  traceparent=%s\n", tracing->traceparent.c_str());
  }

  // Warnings the spec makes a SHOULD, kept out of validate() so they never
  // reject an event the spec permits.
  for (const auto& warning : subject.lint()) {
    std::printf("  lint: %s: %s\n", warning.attribute.c_str(), warning.message.c_str());
  }
}

[[nodiscard]] auto receive(const ce::v1::message& request) -> int {
  switch (ce::v1::http::detect_content_mode(request)) {
    case ce::v1::content_mode::batched: {
      auto events = ce::v1::http::from_batch_message<codec>(request);
      if (!events) {
        std::fprintf(stderr, "batch: %s\n", events.error().detail.c_str());
        return 1;
      }
      std::printf("batched: %zu event(s)\n", events->size());
      for (const auto& subject : *events) {
        describe(subject);
      }
      return 0;
    }
    case ce::v1::content_mode::structured:
    case ce::v1::content_mode::binary_mode: {
      auto subject = ce::v1::http::from_message<codec>(request);
      if (!subject) {
        // not_a_cloudevent is kept distinct from a malformed event, because a
        // receiver usually passes the first through rather than rejecting it.
        if (subject.error().code == ce::v1::errc::not_a_cloudevent) {
          std::printf("not a CloudEvent; passing it along unchanged\n");
          return 0;
        }
        std::fprintf(stderr, "malformed at %s: %s\n", subject.error().where.c_str(),
                     subject.error().detail.c_str());
        return 1;
      }
      std::printf("single event\n");
      describe(*subject);
      return 0;
    }
  }
  return 1;
}

[[nodiscard]] auto binary_request() -> ce::v1::message {
  ce::v1::message request;
  request.header_fields.add("ce-specversion", "1.0");
  request.header_fields.add("ce-id", "A234-1234-1234");
  request.header_fields.add("ce-source", "https://example.test/orders");
  request.header_fields.add("ce-type", "com.example.order.placed");
  request.header_fields.add("ce-time", "2026-09-20T12:34:56Z");
  request.header_fields.add("ce-traceparent",
                            "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
  request.header_fields.add("Content-Type", "application/json");
  request.body = ce::v1::http::detail::to_bytes(R"({"total":42})");
  return request;
}

[[nodiscard]] auto structured_request() -> ce::v1::message {
  ce::v1::message request;
  request.header_fields.add("Content-Type", "application/cloudevents+json");
  request.body = ce::v1::http::detail::to_bytes(
      R"({"specversion":"1.0","id":"B1","source":"/s","type":"com.example.other"})");
  return request;
}

[[nodiscard]] auto not_an_event() -> ce::v1::message {
  ce::v1::message request;
  request.header_fields.add("Content-Type", "application/json");
  request.body = ce::v1::http::detail::to_bytes(R"({"hello":"world"})");
  return request;
}

}  // namespace

int main() {
  int failures = 0;
  for (const auto& request : {binary_request(), structured_request(), not_an_event()}) {
    failures += receive(request);
    std::printf("\n");
  }
  return failures;
}
