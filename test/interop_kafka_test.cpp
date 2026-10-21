#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <boost/ut.hpp>

#include <nlohmann/json.hpp>

#include <cloudevents/binding/common.hpp>
#include <cloudevents/binding/kafka.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "codecs_under_test.hpp"
#include "equality.hpp"
#include "fixtures_path.hpp"

// Kafka records recorded from the Go and Java CloudEvents SDKs, and the records
// this SDK produced for them to read (SWR-SEC-0008, STK-INTEROP-0001).
//
// Neither SDK needs a broker: each converts an event to and from the in-memory
// key, headers and value, and interop/kafka writes those bytes as JSON. This
// suite proves one direction, that their records decode here to the event they
// encode. The other direction is proven by interop/run.sh, which feeds
// test/fixtures/interop/kafka/cpp to both SDKs and fails if either one decodes a
// record to a different event.

namespace {

using namespace std::string_view_literals;

enum class sdk : std::uint8_t { go, java, cpp };

[[nodiscard]] constexpr auto directory_of(sdk producer) -> std::string_view {
  switch (producer) {
    case sdk::go:
      return "go"sv;
    case sdk::java:
      return "java"sv;
    case sdk::cpp:
      return "cpp"sv;
  }
  return {};
}

constexpr std::array decoded_producers{sdk::go, sdk::java};

constexpr std::array modes{
    std::pair{"binary"sv, ce::content_mode::binary_mode},
    std::pair{"structured"sv, ce::content_mode::structured},
};

constexpr std::string_view structured_content_type = "application/cloudevents+json"sv;
constexpr std::string_view content_type_name = "content-type"sv;
constexpr std::string_view attribute_prefix = "ce_"sv;
constexpr std::string_view partition_key = "partitionkey"sv;
constexpr std::string_view keyed_partition = "customer-42"sv;

/// The cases every SDK was asked for. Java alone also writes the keyed event
/// without a key, which is what its writer does when it is not told one.
constexpr std::array shared_names{
    "minimal"sv,
    "full"sv,
    "extensions"sv,
    "text_data"sv,
    "binary_data"sv,
    "time_nanoseconds"sv,
    "extension_types"sv,
    "unicode"sv,
    "percent_in_subject"sv,
    "data_array"sv,
    "minimal_relative"sv,
};
constexpr std::string_view unkeyed_name = "extensions_unkeyed"sv;

/// One record as an SDK wrote it. A null value is not an empty one: to a broker a
/// record with a null value is a tombstone.
struct recorded {
  ce::kafka::record on_the_wire = {};
  bool value_was_null = false;
};

[[nodiscard]] auto read_file(const std::string& path) -> std::string {
  std::ifstream in{path, std::ios::binary};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

[[nodiscard]] auto text_of(const ce::binary& bytes) -> std::string {
  std::string out;
  out.reserve(bytes.size());
  for (const std::byte one : bytes) {
    out.push_back(static_cast<char>(one));
  }
  return out;
}

[[nodiscard]] auto bytes_of(std::string_view text) -> ce::binary {
  ce::binary out;
  out.reserve(text.size());
  for (const char character : text) {
    out.push_back(static_cast<std::byte>(character));
  }
  return out;
}

[[nodiscard]] auto decode_text(const nlohmann::json& encoded) -> std::string {
  auto decoded = ce::base64_decode(encoded.get<std::string>());
  return decoded ? text_of(*decoded) : std::string{};
}

[[nodiscard]] auto read_record(sdk producer, std::string_view name, std::string_view mode)
    -> std::optional<recorded> {
  const std::string raw =
      read_file(ce_fixtures::path("interop/kafka/" + std::string{directory_of(producer)} + "/" +
                                  std::string{name} + "." + std::string{mode} + ".json"));
  const auto document = nlohmann::json::parse(raw, nullptr, false, false);
  if (document.is_discarded()) {
    return std::nullopt;
  }
  recorded out;
  if (!document["key"].is_null()) {
    out.on_the_wire.key = decode_text(document["key"]);
  }
  for (const auto& header : document["headers"]) {
    out.on_the_wire.value.header_fields.add(header[0].get<std::string>(), decode_text(header[1]));
  }
  out.value_was_null = document["value"].is_null();
  if (!out.value_was_null) {
    out.on_the_wire.value.body = bytes_of(decode_text(document["value"]));
  }
  return out;
}

[[nodiscard]] auto read_golden(sdk producer, std::string_view name) -> std::string {
  return read_file(ce_fixtures::path("interop/" + std::string{directory_of(producer)} + "/" +
                                     std::string{name} + ".json"));
}

/// The JSON-format golden that holds the event a record encodes, where the SDK
/// wrote one. Java wrote no document for the percent case.
[[nodiscard]] auto golden_name(sdk producer, std::string_view name)
    -> std::optional<std::string_view> {
  if (name == unkeyed_name) {
    return "extensions"sv;
  }
  if (producer == sdk::java && name == "percent_in_subject"sv) {
    return std::nullopt;
  }
  return name;
}

[[nodiscard]] auto names_of(sdk producer) -> std::vector<std::string_view> {
  std::vector<std::string_view> out{shared_names.begin(), shared_names.end()};
  if (producer == sdk::java) {
    out.push_back(unkeyed_name);
  }
  return out;
}

/// The payload bytes, however an event holds them. The JSON format lets a
/// non-JSON payload travel as a string or as data_base64, so the bytes are what
/// must agree, not the representation (D-INTEROP-1).
[[nodiscard]] auto payload_text(const ce::event& subject) -> std::string {
  if (const auto* text = std::get_if<std::string>(&subject.data())) {
    return *text;
  }
  if (const auto* bytes = std::get_if<ce::binary>(&subject.data())) {
    return text_of(*bytes);
  }
  if (const auto* json = std::get_if<ce::json_text>(&subject.data())) {
    return json->raw;
  }
  if (const auto* document = std::get_if<ce::json_document>(&subject.data())) {
    return document->dump();
  }
  return {};
}

[[nodiscard]] auto rendered_extensions(const ce::event& subject)
    -> std::map<std::string, std::string> {
  std::map<std::string, std::string> out;
  for (const auto& [name, value] : subject.extensions()) {
    out.emplace(std::string{name.view()}, ce::binding::render_attribute(value));
  }
  return out;
}

/// What the record key must be: the SDK's own rule, pinned per SDK.
[[nodiscard]] auto expected_key(sdk producer,
                                std::string_view name,
                                const std::map<std::string, std::string>& extensions)
    -> std::optional<std::string> {
  if (producer == sdk::go) {
    // The Go writer takes the key from the partitionkey extension on its own.
    const auto found = extensions.find(std::string{partition_key});
    return found == extensions.end() ? std::nullopt : std::optional<std::string>{found->second};
  }
  // The Java writer never derives a key: it is the caller's to pass, and the
  // generator passes the partitionkey for "extensions" only.
  return name == "extensions"sv ? std::optional<std::string>{std::string{keyed_partition}}
                                : std::nullopt;
}

template<class C>
void check_record_decodes(std::string_view label,
                          sdk producer,
                          std::string_view name,
                          std::pair<std::string_view, ce::content_mode> mode) {
  using namespace boost::ut;
  const std::string where = std::string{label} + ": " + std::string{directory_of(producer)} + "/" +
                            std::string{name} + "." + std::string{mode.first};

  const auto wire = read_record(producer, name, mode.first);
  expect(wire.has_value()) << where << " is missing";
  if (!wire) {
    return;
  }

  // The mode the record declares is the mode that was asked for.
  expect(ce::kafka::detect_content_mode(wire->on_the_wire.value) == mode.second) << where;

  auto from_record = ce::kafka::from_message<C>(wire->on_the_wire.value);
  expect(bool{from_record}) << where << " did not decode";
  if (!from_record) {
    return;
  }

  const auto golden = golden_name(producer, name);
  if (golden) {
    auto from_document = ce::json_format<C>::decode(read_golden(producer, *golden));
    expect(bool{from_document}) << where << " golden did not decode";
    if (from_document) {
      expect(from_record->id() == from_document->id()) << where;
      expect(bool{from_record->source() == from_document->source()}) << where;
      expect(from_record->type() == from_document->type()) << where;
      expect(ce_test::equal(from_record->subject(), from_document->subject())) << where;
      expect(ce_test::equal(from_record->time(), from_document->time())) << where;
      expect(ce_test::equal(from_record->dataschema(), from_document->dataschema())) << where;
      expect(ce_test::equal(from_record->datacontenttype(), from_document->datacontenttype()))
          << where;
      // By their text, because binary mode carries every extension as a header
      // string and the JSON document carries the integer a number.
      expect(ce_test::equal(rendered_extensions(*from_record), rendered_extensions(*from_document)))
          << where;
      expect(payload_text(*from_record) == payload_text(*from_document)) << where;
    }
  }

  expect(ce_test::equal(wire->on_the_wire.key,
                        expected_key(producer, name, rendered_extensions(*from_record))))
      << where << " record key";
}

template<class C>
void check_every_record_decodes(std::string_view label) {
  for (const auto producer : decoded_producers) {
    for (const auto name : names_of(producer)) {
      for (const auto& mode : modes) {
        check_record_decodes<C>(label, producer, name, mode);
      }
    }
  }
}

/// Header names are the rule D-KAFKA-2 sets: lowercase, the `ce_` prefix, compared
/// byte for byte. Every record an SDK wrote must already follow it.
void check_header_names(std::string_view label) {
  using namespace boost::ut;
  for (const auto producer : {sdk::go, sdk::java, sdk::cpp}) {
    for (const auto name : names_of(producer)) {
      const auto binary_record = read_record(producer, name, "binary");
      const auto structured_record = read_record(producer, name, "structured");
      expect(binary_record.has_value() && structured_record.has_value())
          << label << ": " << directory_of(producer) << "/" << name;
      if (!binary_record || !structured_record) {
        continue;
      }
      for (const auto& [header, value] : binary_record->on_the_wire.value.header_fields) {
        const bool lowercase = std::ranges::none_of(
            header, [](char letter) { return letter >= 'A' && letter <= 'Z'; });
        const bool named_by_the_binding =
            header == content_type_name || std::string_view{header}.starts_with(attribute_prefix);
        expect(lowercase && named_by_the_binding)
            << label << ": " << directory_of(producer) << "/" << name << " header " << header;
      }
      // Structured mode carries the whole event in the value, so the only header
      // is the content type.
      const auto& fields = structured_record->on_the_wire.value.header_fields;
      expect(fields.size() == 1U) << label << ": " << directory_of(producer) << "/" << name;
      const std::string* declared = fields.find_exact(content_type_name);
      expect(declared != nullptr && *declared == structured_content_type)
          << label << ": " << directory_of(producer) << "/" << name;
    }
  }
}

/// The header names this SDK writes in binary mode are the names Go and Java
/// write for the same event, in any order.
void check_header_names_match_the_other_sdks(std::string_view label) {
  using namespace boost::ut;
  const auto names_in = [](const recorded& wire) {
    std::multiset<std::string> out;
    for (const auto& [header, value] : wire.on_the_wire.value.header_fields) {
      out.insert(header);
    }
    return out;
  };
  for (const auto name : shared_names) {
    const auto ours = read_record(sdk::cpp, name, "binary");
    expect(ours.has_value()) << label << ": cpp/" << name;
    if (!ours) {
      continue;
    }
    for (const auto other : decoded_producers) {
      const auto theirs = read_record(other, name, "binary");
      expect(theirs.has_value()) << label << ": " << directory_of(other) << "/" << name;
      if (theirs) {
        expect(ce_test::equal(names_in(*ours), names_in(*theirs)))
            << label << ": " << directory_of(other) << "/" << name;
      }
    }
  }
}

}  // namespace

// spec: SWR-SEC-0008
const boost::ut::suite<"interop-kafka-records-decode"> interop_kafka_records_decode = [] {
  using namespace boost::ut;
  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec} + " decodes every Go and Java record to the event it encodes") =
        [codec] { check_every_record_decodes<C>(codec); };
  });
};

// spec: SWR-SEC-0008
const boost::ut::suite<"interop-kafka-header-names"> interop_kafka_header_names = [] {
  using namespace boost::ut;
  "every SDK writes lowercase ce_ headers and one content-type header in structured mode"_test =
      [] { check_header_names("header names"); };
  "this SDK writes the header names Go and Java write for the same event"_test = [] {
    check_header_names_match_the_other_sdks("header names");
  };
};

// spec: SWR-SEC-0008
const boost::ut::suite<"interop-kafka-differences"> interop_kafka_differences = [] {
  using namespace boost::ut;

  // The Go writer derives the record key from the partitionkey extension and the
  // Java writer does not. This SDK derives none unless the caller names a mapper
  // (SWR-KAFKA-0009), which is the Java behaviour: the keys differ by default.
  "go keys a partitionkey event and java leaves it unkeyed unless it is told"_test = [] {
    const auto go = read_record(sdk::go, "extensions", "binary");
    const auto java = read_record(sdk::java, "extensions_unkeyed", "binary");
    expect(go.has_value() && java.has_value());
    if (go && java) {
      expect(ce_test::equal(go->on_the_wire.key, std::optional<std::string>{keyed_partition}));
      expect(!java->on_the_wire.key.has_value());
      // The attribute still travels as a header in both (SWR-KAFKA-0009).
      expect(go->on_the_wire.value.header_fields.contains_exact("ce_partitionkey"));
      expect(java->on_the_wire.value.header_fields.contains_exact("ce_partitionkey"));
    }
  };

  // An event without a payload is a record with a NULL value from both SDKs, a
  // tombstone to a compacted topic. This SDK writes an empty, non-null value.
  "go and java write a null value for an event without a payload"_test = [] {
    for (const auto producer : decoded_producers) {
      const auto wire = read_record(producer, "minimal", "binary");
      expect(wire.has_value());
      if (wire) {
        expect(wire->value_was_null) << directory_of(producer);
        expect(wire->on_the_wire.value.body.empty()) << directory_of(producer);
      }
    }
    const auto ours = read_record(sdk::cpp, "minimal", "binary");
    expect(ours.has_value());
    if (ours) {
      expect(!ours->value_was_null);
      expect(ours->on_the_wire.value.body.empty());
    }
  };

  // Kafka has no escaping rule for a header value, unlike HTTP (D-HTTP-1), so a
  // literal percent is already the interoperable spelling.
  "a literal percent in a header value is read as it was written"_test = [] {
    for (const auto producer : decoded_producers) {
      const auto wire = read_record(producer, "percent_in_subject", "binary");
      expect(wire.has_value());
      if (!wire) {
        continue;
      }
      auto decoded = ce::kafka::from_message<ce::codec::nlohmann_codec>(wire->on_the_wire.value);
      expect(bool{decoded}) << directory_of(producer);
      if (decoded) {
        expect(ce_test::equal(decoded->subject(), std::optional<std::string>{"100% of 50%OFF"}))
            << directory_of(producer);
        expect(rendered_extensions(*decoded).at("pct") == "a%20b") << directory_of(producer);
      }
    }
  };

  // interop/kafka/go and interop/kafka/java probe the same two records and fail
  // if their answer changes: Go reads both, Java refuses both (D-INTEROP-5).
  // This SDK compares header names byte for byte (D-KAFKA-2), so it agrees with
  // Java and refuses what Go accepts.
  "capitalised header names are refused here, as in java and unlike go"_test = [] {
    const ce::message capital_prefix{
        .header_fields = {
            {"CE_specversion", "1.0"}, {"CE_id", "1"}, {"CE_source", "/s"}, {"CE_type", "t"}}};
    auto binary = ce::kafka::from_message<ce::codec::nlohmann_codec>(capital_prefix);
    expect(!binary);
    if (!binary) {
      expect(binary.error().code == ce::errc::not_a_cloudevent);
    }

    const std::string document = R"({"specversion":"1.0","id":"1","source":"/s","type":"t"})";
    const ce::message capital_content_type{
        .header_fields = {{"Content-Type", std::string{structured_content_type}}},
        .body = bytes_of(document)};
    auto structured = ce::kafka::from_message<ce::codec::nlohmann_codec>(capital_content_type);
    expect(!structured);
    if (!structured) {
      expect(structured.error().code == ce::errc::not_a_cloudevent);
    }
  };
};

int main() {}
