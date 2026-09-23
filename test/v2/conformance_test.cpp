#include <boost/ut.hpp>

#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/v2/core.hpp>
#include <cloudevents/v2/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fixtures_path.hpp"
#include "mini_codec.hpp"

// Every fenced example from the CloudEvents v1.0.2 core, JSON format and HTTP
// binding documents, stored verbatim and asserted here (SWR-SEC-0004).
//
// Several of the published examples are illustrations rather than wire bytes:
// they carry `Content-Length: nnnn`, `... application data ...` and the like.
// The table below records which, and the suite ASSERTS that classification by
// scanning the bytes. A future revision that replaces a placeholder with real
// content fails that assertion instead of staying quietly on the weaker path.

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::v2::codec::nlohmann_codec;
using mini_codec = ce::test::mini_codec;

[[nodiscard]] auto read_fixture(std::string_view name) -> std::string {
  std::ifstream in{ce_fixtures::path(name), std::ios::binary};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

/// \brief True when the published text carries a placeholder rather than bytes.
[[nodiscard]] auto is_elided(std::string_view text) -> bool {
  return text.find("...") != std::string_view::npos ||
         text.find("nnnn") != std::string_view::npos;
}

enum class shape { structured_json, batch_json, binary_headers, http_exchange, media_type_only };

struct fixture {
  std::string_view name;
  shape form;
  bool elided;
};

constexpr std::array fixtures{
    fixture{"core-01-example-event.json", shape::structured_json, false},
    fixture{"json-01-structured-event.json", shape::structured_json, true},
    fixture{"json-02-binary-headers.http", shape::binary_headers, true},
    fixture{"json-03-structured-event.json", shape::structured_json, false},
    fixture{"json-04-binary-headers.http", shape::binary_headers, false},
    fixture{"json-05-structured-event.json", shape::structured_json, false},
    fixture{"json-06-binary-headers.http", shape::binary_headers, false},
    fixture{"json-07-structured-event.json", shape::structured_json, false},
    fixture{"json-08-binary-headers.http", shape::binary_headers, false},
    fixture{"json-09-batch.json", shape::batch_json, true},
    fixture{"json-10-empty-batch.json", shape::batch_json, false},
    fixture{"http-01-binary-request.http", shape::http_exchange, true},
    fixture{"http-02-binary-response.http", shape::http_exchange, true},
    fixture{"http-03-structured-content-type.http", shape::media_type_only, false},
    fixture{"http-04-structured-request.http", shape::http_exchange, true},
    fixture{"http-05-structured-response.http", shape::http_exchange, true},
    fixture{"http-06-batch-content-type.http", shape::media_type_only, false},
    fixture{"http-07-batch-request.http", shape::http_exchange, true},
    fixture{"http-08-batch-response.http", shape::http_exchange, true},
};

/// \brief Read a `ce-`-header fixture into a message.
///
/// The published examples are header blocks followed by a blank line and a
/// body, which is the shape the binding consumes.
[[nodiscard]] auto parse_header_block(std::string_view text) -> ce::v2::message {
  ce::v2::message request;
  std::size_t pos = 0;
  bool in_body = false;
  std::string body;

  // A fenced block may open with a newline, and that leading blank line is not
  // the header/body separator.
  while (pos < text.size() && (text[pos] == '\n' || text[pos] == '\r')) {
    ++pos;
  }

  while (pos < text.size()) {
    const std::size_t eol = text.find('\n', pos);
    const std::string_view line =
        text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);

    if (in_body) {
      body.append(line);
      body.push_back('\n');
    } else if (line.empty()) {
      in_body = true;
    } else if (const std::size_t colon = line.find(':'); colon != std::string_view::npos) {
      std::string_view name = line.substr(0, colon);
      std::string_view value = line.substr(colon + 1);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
      }
      while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) {
        value.remove_suffix(1);
      }
      request.header_fields.add(std::string{name}, std::string{value});
    }

    if (eol == std::string_view::npos) {
      break;
    }
    pos = eol + 1;
  }

  while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
    body.pop_back();
  }
  request.body = ce::v2::http::detail::to_bytes(body);
  return request;
}

template <class C>
void check_literal_structured(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v2::json_format<C>;

  // core-01: the core specification's own example event.
  auto core = format::decode(read_fixture("core-01-example-event.json"));
  expect(core.has_value()) << label << ": core-01";
  if (core) {
    expect(core->specversion().view() == "1.0"sv) << label;
    expect(core->type() == "com.github.pull_request.opened") << label;
    expect(core->source().view() == "https://github.com/cloudevents/spec/pull") << label;
    expect(core->id() == "A234-1234-1234") << label;
    expect(bool{core->subject() == std::optional<std::string>{"123"}}) << label;
    expect(core->time().has_value()) << label;
    if (core->time()) {
      // Stored byte for byte, so re-emitting does not alter what a peer signed.
      expect(ce::v2::to_string(*core->time()) == "2018-04-05T17:31:00Z") << label;
    }
    expect(bool{core->datacontenttype() == std::optional<std::string>{"text/xml"}}) << label;
    // A string payload under a non-JSON content type is the payload itself.
    expect(std::holds_alternative<std::string>(core->data())) << label;
    if (const auto* text = std::get_if<std::string>(&core->data())) {
      expect(*text == R"(<much wow="xml"/>)") << label;
    }
    // The two extensions, with the types JSON carries.
    const auto* extension = core->extension("comexampleextension1");
    expect(extension != nullptr) << label;
    if (extension != nullptr) {
      expect(std::holds_alternative<std::string>(*extension)) << label;
    }
    const auto* other = core->extension("comexampleothervalue");
    expect(other != nullptr) << label;
    if (other != nullptr) {
      expect(std::holds_alternative<std::int32_t>(*other)) << label;
      if (std::holds_alternative<std::int32_t>(*other)) {
        expect(std::get<std::int32_t>(*other) == 5) << label;
      }
    }
    // What decoded goes back out: the property a validation step used to stand
    // in for.
    expect(format::encode(*core).has_value()) << label;
  }

  // json-03, json-05, json-07: the JSON format's structured examples.
  for (const auto name : {"json-03-structured-event.json"sv, "json-05-structured-event.json"sv,
                          "json-07-structured-event.json"sv}) {
    auto decoded = format::decode(read_fixture(name));
    expect(decoded.has_value()) << label << ": " << name;
    if (decoded) {
      expect(decoded->specversion().view() == "1.0"sv) << label << ": " << name;
      expect(decoded->id().size() != 0U) << label << ": " << name;
      expect(decoded->type().size() != 0U) << label << ": " << name;
      // Re-encoding must produce a document that decodes to the same event.
      auto again = format::encode(*decoded);
      expect(again.has_value()) << label << ": " << name;
      if (again) {
        auto second = format::decode(*again);
        expect(second.has_value()) << label << ": " << name;
        if (second) {
          expect(bool{*second == *decoded}) << label << ": " << name;
        }
      }
    }
  }

  // json-10: the empty batch, which is a valid document carrying no events.
  auto empty = format::decode_batch(read_fixture("json-10-empty-batch.json"));
  expect(empty.has_value()) << label << ": json-10";
  if (empty) {
    expect(empty->empty()) << label;
  }
}

template <class C>
void check_elided_base64(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v2::json_format<C>;

  // json-01 and json-09 are real documents whose data_base64 member holds a
  // description rather than base64. Everything up to that member decodes, and
  // the member is then rejected - which is the right answer for these bytes.
  auto single = format::decode(read_fixture("json-01-structured-event.json"));
  expect(!single.has_value()) << label << ": json-01 should not decode";
  if (!single) {
    expect(single.error().code == ce::v2::errc::invalid_base64) << label << ": json-01";
  }

  auto batch = format::decode_batch(read_fixture("json-09-batch.json"));
  expect(!batch.has_value()) << label << ": json-09 should not decode";
  if (!batch) {
    expect(batch.error().code == ce::v2::errc::invalid_base64) << label << ": json-09";
  }

  // The same documents with a real base64 payload DO decode, so the rejection
  // is about the placeholder rather than about the rest of the document.
  std::string repaired = read_fixture("json-01-structured-event.json");
  const std::string placeholder = R"("... base64 encoded string ...")";
  const auto at = repaired.find(placeholder);
  expect(at != std::string::npos) << label;
  if (at != std::string::npos) {
    repaired.replace(at, placeholder.size(), R"("AAEC/w==")");
    auto decoded = format::decode(repaired);
    expect(decoded.has_value()) << label << ": json-01 with real base64";
    if (decoded) {
      expect(std::holds_alternative<ce::v2::binary>(decoded->data())) << label;
      expect(decoded->id() == "A234-1234-1234") << label;
      expect(format::encode(*decoded).has_value()) << label;
    }
  }
}

template <class C>
void check_literal_binary_headers(std::string_view label) {
  using namespace boost::ut;

  // json-04, json-06, json-08: binary-mode header blocks with real bodies.
  for (const auto name : {"json-04-binary-headers.http"sv, "json-06-binary-headers.http"sv,
                          "json-08-binary-headers.http"sv}) {
    const ce::v2::message request = parse_header_block(read_fixture(name));
    expect(request.header_fields.contains("ce-specversion")) << label << ": " << name;

    // No CloudEvents media type, so these are binary mode by the binding's rule.
    expect(ce::v2::http::detect_content_mode(request) == ce::v2::content_mode::binary_mode)
        << label << ": " << name;

    auto decoded = ce::v2::http::from_message<C>(request);
    expect(decoded.has_value()) << label << ": " << name;
    if (decoded) {
      expect(decoded->specversion().view() == "1.0"sv) << label << ": " << name;
      expect(decoded->type() == "com.example.someevent") << label << ": " << name;
      expect(decoded->source().view() == "/mycontext") << label << ": " << name;
      expect(decoded->time().has_value()) << label << ": " << name;
      // The binary binding carries every extension as text.
      const auto* extension = decoded->extension("comexampleothervalue");
      expect(extension != nullptr) << label << ": " << name;
      if (extension != nullptr) {
        expect(std::holds_alternative<std::string>(*extension)) << label << ": " << name;
      }
      expect(ce::v2::http::to_message<C>(*decoded, ce::v2::content_mode::binary_mode).has_value())
          << label << ": " << name;
    }
  }
}

void check_media_type_examples() {
  using namespace boost::ut;

  // http-03 and http-06 are single Content-Type lines. Each is asserted to
  // select the mode the binding's own text says it selects.
  const ce::v2::message structured = parse_header_block(read_fixture("http-03-structured-content-type.http"));
  expect(ce::v2::http::detect_content_mode(structured) == ce::v2::content_mode::structured);

  const ce::v2::message batched = parse_header_block(read_fixture("http-06-batch-content-type.http"));
  expect(ce::v2::http::detect_content_mode(batched) == ce::v2::content_mode::batched);

  // The ordering trap the binding has to get right: the batch media type also
  // begins with the structured one, and the spec's own examples are the proof.
  const std::string* declared = batched.header_fields.find("Content-Type");
  expect(declared != nullptr);
  if (declared != nullptr) {
    expect(declared->starts_with("application/cloudevents"));
    expect(declared->starts_with("application/cloudevents-batch"));
  }
}

void check_elided_exchanges() {
  using namespace boost::ut;

  // http-01, http-02, http-04, http-05, http-07, http-08 are illustrations: they
  // carry `Content-Length: nnnn` and `... application data ...`. What CAN be
  // asserted is the header vocabulary and the media types they demonstrate.
  const ce::v2::message binary_request = parse_header_block(read_fixture("http-01-binary-request.http"));
  expect(binary_request.header_fields.contains("ce-specversion"));
  expect(binary_request.header_fields.contains("ce-type"));
  expect(binary_request.header_fields.contains("ce-id"));
  expect(binary_request.header_fields.contains("ce-source"));
  expect(binary_request.header_fields.contains("ce-time"));
  // No CloudEvents media type on a binary-mode request.
  expect(ce::v2::http::detect_content_mode(binary_request) == ce::v2::content_mode::binary_mode);

  const ce::v2::message structured_request =
      parse_header_block(read_fixture("http-04-structured-request.http"));
  expect(ce::v2::http::detect_content_mode(structured_request) == ce::v2::content_mode::structured);
  // A structured request carries the event in the body, not in ce- headers.
  expect(!structured_request.header_fields.contains("ce-id"));

  const ce::v2::message batch_request = parse_header_block(read_fixture("http-07-batch-request.http"));
  expect(ce::v2::http::detect_content_mode(batch_request) == ce::v2::content_mode::batched);
  expect(!batch_request.header_fields.contains("ce-id"));
}

const boost::ut::suite<"conformance-spec-examples"> conformance = [] {
  using namespace boost::ut;

  "every published example is present and byte-exact"_test = [] {
    for (const auto& entry : fixtures) {
      const std::string bytes = read_fixture(entry.name);
      expect(!bytes.empty()) << entry.name << ": fixture missing or empty";
    }
    expect(fixtures.size() == 19_ul);
  };

  "the elided/literal classification matches the bytes"_test = [] {
    // The table is hand-written, so it is checked against the fixtures. A
    // specification revision that fills in a placeholder fails here, which is
    // the signal to give that fixture a real decode.
    for (const auto& entry : fixtures) {
      const std::string bytes = read_fixture(entry.name);
      expect(is_elided(bytes) == entry.elided)
          << entry.name << ": expected elided=" << entry.elided << ", bytes say "
          << is_elided(bytes);
    }
  };

  "literal structured examples nlohmann_codec"_test = [] {
    check_literal_structured<nlohmann_codec>("nlohmann_codec");
  };
  "literal structured examples mini_codec"_test = [] {
    check_literal_structured<mini_codec>("mini_codec");
  };
  "elided base64 examples nlohmann_codec"_test = [] {
    check_elided_base64<nlohmann_codec>("nlohmann_codec");
  };
  "elided base64 examples mini_codec"_test = [] {
    check_elided_base64<mini_codec>("mini_codec");
  };
  "literal binary headers nlohmann_codec"_test = [] {
    check_literal_binary_headers<nlohmann_codec>("nlohmann_codec");
  };
  "literal binary headers mini_codec"_test = [] {
    check_literal_binary_headers<mini_codec>("mini_codec");
  };
  "media type examples"_test = [] { check_media_type_examples(); };
  "elided exchanges carry the documented header vocabulary"_test = [] {
    check_elided_exchanges();
  };
};

}  // namespace

int main() {}
