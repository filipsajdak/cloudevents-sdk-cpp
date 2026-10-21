#include <string>
#include <string_view>
#include <variant>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/decode_options.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/v3/binding/http.hpp>
#include <cloudevents/v3/core.hpp>
#include <cloudevents/v3/format/json_format.hpp>

#include "mini_codec.hpp"

namespace {

using namespace std::string_view_literals;
using codec = ce::test::mini_codec;

[[nodiscard]] auto binary_request(std::string_view body) -> ce::message {
  ce::message request{.header_fields = {{"ce-specversion", "1.0"},
                                        {"ce-id", "1"},
                                        {"ce-source", "/s"},
                                        {"ce-type", "t"},
                                        {"Content-Type", "application/json"}}};
  request.body = ce::to_bytes(body);
  return request;
}

// spec: SWR-BIND-0013
const boost::ut::suite<"v3-ignores-parse-binary-body"> v3_ignores_parse_binary_body = [] {
  using namespace boost::ut;

  "the v3 binary reader stores the body unparsed, whatever the shared struct says"_test = [] {
    // ce::v3 takes no options on its binary entry points: nothing a caller sets
    // reaches it, and the body is its own text, valid or not, as in v0.5.0.
    for (const auto body : {R"({"a":[1,2]})"sv, "{not json"sv}) {
      const auto request = binary_request(body);
      const auto received = ce::v3::http::from_message<codec>(request);
      expect(received.has_value()) << body;
      if (received) {
        const auto* text = std::get_if<ce::v3::json_text>(&received->data());
        expect(text != nullptr) << body;
        if (text != nullptr) {
          expect(text->raw == body) << body;
        }
      }
    }
  };

  "the v3 structured decoders behave the same with the field set either way"_test = [] {
    using format = ce::v3::json_format<codec>;
    constexpr auto text = R"({"specversion":"1.0","id":"1","source":"/s","type":"t",)"
                          R"("datacontenttype":"application/json","data":{"a":[1,2]}})"sv;
    for (const bool parse : {true, false}) {
      const auto decoded = format::decode(std::string{text}, {.parse_binary_body = parse});
      expect(decoded.has_value() && std::holds_alternative<ce::v3::json_document>(decoded->data()))
          << parse;
      const auto as_text = format::decode(std::string{text},
                                          {.retain_document_up_to = 0, .parse_binary_body = parse});
      expect(as_text.has_value() && std::holds_alternative<ce::v3::json_text>(as_text->data()))
          << parse;
      const auto refused = format::decode("{not json", {.parse_binary_body = parse});
      expect(!refused.has_value()) << parse;
    }
  };
};

}  // namespace

int main() {}
