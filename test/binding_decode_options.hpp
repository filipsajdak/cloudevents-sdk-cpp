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

/// \brief With `parse_binary_body` false a binary-mode JSON body is kept as its own bytes
/// (SWR-BIND-0012).
///
/// The same drive as above: `lay_out` and `read` are the binding's `to_message` and
/// `from_message`. The body is replaced after the layout so a malformed one reaches the reader.
template<class Codec, class LayOut, class Read>
void check_binary_body_not_parsed(std::string_view label, LayOut lay_out, Read read) {
  using namespace boost::ut;
  constexpr ce::json::decode_options unparsed{.parse_binary_body = false};

  auto laid_out = lay_out(options_event(), ce::content_mode::binary_mode);
  expect(laid_out.has_value()) << label;
  if (!laid_out) {
    return;
  }
  const auto holds_text_of = [&label](const ce::result<ce::event>& got, std::string_view expected) {
    const auto* text = got.has_value() ? std::get_if<ce::json_text>(&got->data()) : nullptr;
    expect(text != nullptr) << label;
    if (text != nullptr) {
      expect(text->raw == expected) << label << ": the bytes as received";
    }
  };

  holds_text_of(read(*laid_out, unparsed), options_payload);
  for (const std::size_t limit :
       {std::size_t{0}, options_payload.size() - 1U, options_payload.size()}) {
    holds_text_of(
        read(*laid_out,
             ce::json::decode_options{.retain_document_up_to = limit, .parse_binary_body = false}),
        options_payload);
  }

  const auto default_read = read(*laid_out, ce::json::decode_options{});
  expect(default_read.has_value() &&
         std::holds_alternative<ce::json_document>(default_read->data()))
      << label << ": the default parses";
  const auto true_read = read(*laid_out, ce::json::decode_options{.parse_binary_body = true});
  expect(true_read.has_value() && std::holds_alternative<ce::json_document>(true_read->data()))
      << label << ": an explicit true parses";

  // A body that is not JSON: the default refuses it, the opt-in forwards it, at any size.
  constexpr std::string_view malformed = "{not json";
  laid_out->body = ce::to_bytes(malformed);
  const auto refused = read(*laid_out, ce::json::decode_options{});
  expect(!refused.has_value() && refused.error().code == ce::errc::parse_error) << label;
  holds_text_of(read(*laid_out, unparsed), malformed);
  holds_text_of(
      read(*laid_out,
           ce::json::decode_options{.retain_document_up_to = 0, .parse_binary_body = false}),
      malformed);

  const std::string huge =
      "{" + std::string(ce::json::decode_options::default_retention_limit, ' ');
  laid_out->body = ce::to_bytes(huge);
  holds_text_of(read(*laid_out, unparsed), huge);

  // An empty body is still no payload.
  laid_out->body = {};
  const auto empty = read(*laid_out, unparsed);
  expect(empty.has_value() && std::holds_alternative<std::monostate>(empty->data())) << label;
}

/// \brief Structured decode ignores `parse_binary_body` (SWR-BIND-0013).
template<class Codec, class LayOut, class Read>
void check_structured_ignores_parse_binary_body(std::string_view label, LayOut lay_out, Read read) {
  using namespace boost::ut;
  constexpr ce::json::decode_options unparsed{.parse_binary_body = false};

  auto laid_out = lay_out(options_event(), ce::content_mode::structured);
  expect(laid_out.has_value()) << label;
  if (!laid_out) {
    return;
  }
  const auto parsed = read(*laid_out, unparsed);
  expect(parsed.has_value() && std::holds_alternative<ce::json_document>(parsed->data()))
      << label << ": a structured event is still parsed into its document";
  const auto text = read(
      *laid_out, ce::json::decode_options{.retain_document_up_to = 0, .parse_binary_body = false});
  expect(text.has_value() && std::holds_alternative<ce::json_text>(text->data())) << label;

  laid_out->body = ce::to_bytes("{not json");
  const auto refused = read(*laid_out, unparsed);
  expect(!refused.has_value()) << label << ": a malformed structured body still fails";
  if (!refused) {
    expect(refused.error().code == ce::errc::parse_error) << label;
  }
}

}  // namespace ce_test
