#pragma once

/// \file
/// \brief What every binding's from_message does with a JSON payload and decode options.
///
/// HTTP, Kafka and NATS share the binding core, so one set of checks drives all three
/// through a pair of callables: how a binding lays an event out, and how it reads a
/// message back.

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/decode_options.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "payload.hpp"

namespace ce_test {

inline constexpr std::string_view options_payload = R"({"a":[1,2],"b":{"c":null}})";

[[nodiscard]] inline auto options_event() -> ce::event {
  using namespace ce::literals;
  return ce::event{"1"_id,
                   "/spec/test"_source,
                   "com.example.thing"_type,
                   {.datacontenttype = "application/json"_mediatype,
                    .data = ce::json_text{.raw = std::string{options_payload}}}};
}

/// \brief `from_message` applies the caller's retention limit in every mode (SWR-BIND-0009).
///
/// `lay_out(event, mode)` returns the binding's message; `read(message, options)` is its
/// `from_message`. Both content modes must keep a document within the limit, keep the text
/// above it, and treat zero as "always text".
template<class Codec, class LayOut, class Read>
void check_from_message_takes_decode_options(std::string_view label, LayOut lay_out, Read read) {
  using namespace boost::ut;

  for (const auto mode : {ce::content_mode::structured, ce::content_mode::binary_mode}) {
    const auto laid_out = lay_out(options_event(), mode);
    expect(laid_out.has_value()) << label;
    if (!laid_out) {
      continue;
    }
    const std::size_t length = laid_out->body.size();

    const auto by_default = read(*laid_out, ce::json::decode_options{});
    expect(by_default.has_value() && std::holds_alternative<ce::json_document>(by_default->data()))
        << label << ": the default keeps a small payload as a document";

    const auto at_limit =
        read(*laid_out, ce::json::decode_options{.retain_document_up_to = length});
    expect(at_limit.has_value() && std::holds_alternative<ce::json_document>(at_limit->data()))
        << label << ": a message of exactly the limit keeps its document";

    const auto over_limit =
        read(*laid_out, ce::json::decode_options{.retain_document_up_to = length - 1U});
    expect(over_limit.has_value() && std::holds_alternative<ce::json_text>(over_limit->data()))
        << label << ": a message one byte over the limit keeps text";

    const auto no_documents = read(*laid_out, ce::json::decode_options{.retain_document_up_to = 0});
    expect(no_documents.has_value() && std::holds_alternative<ce::json_text>(no_documents->data()))
        << label << ": a limit of zero keeps every payload as text";
    expect(no_documents.has_value() &&
           same_json_payload<Codec>(no_documents->data(), options_payload))
        << label << ": the text is the payload";
  }
}

}  // namespace ce_test
