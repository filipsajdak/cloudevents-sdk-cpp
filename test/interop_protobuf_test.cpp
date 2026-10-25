#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/format/protobuf_format.hpp>
#include <cloudevents/result.hpp>

#include "../interop/protobuf/cpp/corpus.hpp"
#include "fixtures_path.hpp"
#include "protobuf_support.hpp"

// Protobuf bytes recorded from the Go and Java CloudEvents SDKs and from protoc, and the bytes
// this SDK produced for them to read (SWR-PROTO-0016, STK-INTEROP-0001).
//
// This suite proves the direction "their bytes decode here to the event they encode". The other
// direction, "our bytes decode there to the event we encoded", is proven by interop/run.sh, which
// feeds test/fixtures/interop/protobuf/cpp to both SDKs and fails if either decodes a file to a
// different event. Neither needs a network, and the comparison is by decoded event, never by
// bytes: the SDKs agree on bytes for one event of thirteen (D-INTEROP-6).

namespace {

using namespace std::string_view_literals;
using namespace ce::test::pb;
namespace corpus = ce::interop::protobuf_corpus;
using protobuf = ce::protobuf_format;

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

[[nodiscard]] auto read_bytes(std::string_view relative) -> std::optional<ce::binary> {
  std::ifstream in{ce_fixtures::path(relative), std::ios::binary};
  if (!in) {
    return std::nullopt;
  }
  const std::vector<char> raw{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
  const auto octets = std::as_bytes(std::span{raw});
  return ce::binary{octets.begin(), octets.end()};
}

[[nodiscard]] auto golden(sdk producer, std::string_view name) -> std::optional<ce::binary> {
  return read_bytes("interop/protobuf/" + std::string{directory_of(producer)} + "/" +
                    std::string{name} + ".pb");
}

[[nodiscard]] auto protoc_case(std::string_view name, std::string_view extension = "pb"sv)
    -> ce::binary {
  return read_bytes("interop/protobuf/protoc/" + std::string{name} + "." + std::string{extension})
      .value_or(ce::binary{});
}

/// The payload as bytes, whichever of text and bytes the format used for it.
[[nodiscard]] auto with_bytes_data(ce::event cloud_event) -> ce::event {
  if (const auto* text = std::get_if<std::string>(&cloud_event.data())) {
    auto media_type = cloud_event.datacontenttype();
    cloud_event.set_data(ce::to_bytes(*text), std::move(media_type));
  } else if (const auto* json = std::get_if<ce::json_text>(&cloud_event.data())) {
    auto media_type = cloud_event.datacontenttype();
    cloud_event.set_data(ce::to_bytes(json->raw), std::move(media_type));
  }
  return cloud_event;
}

/// What the Protobuf wire does to a corpus event, whoever wrote it, and then what one SDK does
/// that the other does not. Each adjustment is a documented loss or a known difference of an SDK
/// (D-PROTO-1, D-INTEROP-6); nothing else may differ.
[[nodiscard]] auto after_wire(sdk producer, const corpus::named_event& entry) -> ce::event {
  using namespace ce::literals;
  ce::event expected = entry.event;
  if (entry.name == "time_nanoseconds"sv) {
    // a timestamp carries an instant: the offset is gone and the digits are the fewest
    expected = ce::event{expected.id(),
                         expected.source(),
                         expected.type(),
                         {.time = corpus::instant("2026-09-20T10:34:56.123456789Z")}};
  }
  if (producer == sdk::go && entry.name == "unicode"sv) {
    // sdk-go parses the source as a URL and writes it percent-encoded
    expected = ce::event{expected.id(),
                         "/interop/protobuf/%C3%A9v%C3%A9nement"_source,
                         expected.type(),
                         {.datacontenttype = expected.datacontenttype(),
                          .subject = expected.subject(),
                          .data = expected.data()}};
  }
  if (producer == sdk::java && entry.name == "proto_data_noschema"sv) {
    // sdk-java writes the type URL of the message it wraps, and the reader keeps it as dataschema
    expected = ce::event{expected.id(),
                         expected.source(),
                         expected.type(),
                         {.datacontenttype = expected.datacontenttype(),
                          .dataschema = "type.googleapis.com/google.protobuf.Struct"_dataschema,
                          .data = expected.data()}};
  }
  return with_bytes_data(std::move(expected));
}

[[nodiscard]] auto decoded_golden(sdk producer, std::string_view name) -> std::optional<ce::event> {
  const auto bytes = golden(producer, name);
  if (!bytes) {
    return std::nullopt;
  }
  auto read = protobuf::decode(*bytes);
  if (!read) {
    return std::nullopt;
  }
  return std::move(*read);
}

}  // namespace

// spec: SWR-PROTO-0016
const boost::ut::suite<"interop-protobuf-goldens-decode"> goldens_decode = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the corpus has the thirteen events all three producers wrote"_test = [] {
    expect(corpus::events().size() == 13U);
    for (const auto producer : {sdk::go, sdk::java, sdk::cpp}) {
      for (const auto& entry : corpus::events()) {
        expect(golden(producer, entry.name).has_value())
            << directory_of(producer) << "/" << entry.name;
      }
    }
  };

  "every Go golden decodes to the event it encodes"_test = [] {
    for (const auto& entry : corpus::events()) {
      const auto read = decoded_golden(sdk::go, entry.name);
      expect(read.has_value()) << entry.name;
      if (read) {
        expect(with_bytes_data(*read) == after_wire(sdk::go, entry)) << "go/" << entry.name;
      }
    }
  };

  "every Java golden decodes to the event it encodes"_test = [] {
    for (const auto& entry : corpus::events()) {
      const auto read = decoded_golden(sdk::java, entry.name);
      expect(read.has_value()) << entry.name;
      if (read) {
        expect(with_bytes_data(*read) == after_wire(sdk::java, entry)) << "java/" << entry.name;
      }
    }
  };
};

// spec: SWR-PROTO-0016
const boost::ut::suite<"interop-protobuf-cpp-bytes-read-by-sdks"> cpp_bytes = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the committed C++ bytes are what the encoder writes today"_test = [] {
    for (const auto& entry : corpus::events()) {
      const auto committed = golden(sdk::cpp, entry.name);
      expect(committed.has_value()) << entry.name;
      expect(committed.has_value() && *committed == encoded_form(entry.event)) << entry.name;
    }
  };

  "they decode here to the event they encode, as the SDKs are asked to"_test = [] {
    for (const auto& entry : corpus::events()) {
      const auto read = decoded_golden(sdk::cpp, entry.name);
      expect(read.has_value()) << entry.name;
      if (read) {
        expect(with_bytes_data(*read) == after_wire(sdk::cpp, entry)) << "cpp/" << entry.name;
      }
    }
  };

  "text is written as text_data, as the specification says"_test = [] {
    for (const auto name :
         {"text_data"sv, "text_data_empty"sv, "full"sv, "unicode"sv, "data_array"sv}) {
      const auto read = decoded_golden(sdk::cpp, name);
      expect(read.has_value() && std::holds_alternative<std::string>(read->data())) << name;
    }
  };
};

// spec: SWR-PROTO-0016
const boost::ut::suite<"interop-protobuf-differences"> differences = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the SDKs agree on bytes for one event of thirteen, and no other"_test = [] {
    std::set<std::string> identical;
    for (const auto& entry : corpus::events()) {
      const auto go = golden(sdk::go, entry.name);
      const auto java = golden(sdk::java, entry.name);
      if (go && java && *go == *java) {
        identical.insert(entry.name);
      }
    }
    expect(identical == std::set<std::string>{"binary_data"});
  };

  "sdk-go never writes text_data: every payload it writes is binary_data"_test = [] {
    for (const auto& entry : corpus::events()) {
      const auto read = decoded_golden(sdk::go, entry.name);
      expect(read.has_value() && !std::holds_alternative<std::string>(read->data())) << entry.name;
    }
  };

  "sdk-java writes text_data for text and JSON, and binary_data for the rest"_test = [] {
    for (const auto name :
         {"text_data"sv, "text_data_empty"sv, "full"sv, "unicode"sv, "data_array"sv}) {
      const auto read = decoded_golden(sdk::java, name);
      expect(read.has_value() && std::holds_alternative<std::string>(read->data())) << name;
    }
    const auto binary = decoded_golden(sdk::java, "binary_data"sv);
    expect(binary.has_value() && std::holds_alternative<ce::binary>(binary->data()));
  };

  "sdk-go writes an empty binary_data for no data, which reads back as no payload"_test = [] {
    const auto go = golden(sdk::go, "minimal"sv);
    const auto java = golden(sdk::java, "minimal"sv);
    expect(go.has_value() && java.has_value());
    if (go && java) {
      constexpr std::array empty_binary_data{std::byte{binary_data_field << 3U | 2U}, std::byte{0}};
      expect(go->size() == java->size() + empty_binary_data.size());
      expect(std::ranges::search(*go, empty_binary_data).begin() != go->end());
    }
    const auto read = decoded_golden(sdk::go, "minimal"sv);
    expect(read.has_value() && std::holds_alternative<std::monostate>(read->data()));
  };

  "sdk-go writes a proto_data type URL from the dataschema, sdk-java from the message"_test = [] {
    const auto go = decoded_golden(sdk::go, "proto_data_noschema"sv);
    const auto java = decoded_golden(sdk::java, "proto_data_noschema"sv);
    expect(go.has_value() && !go->dataschema().has_value());
    expect(java.has_value() && java->dataschema().has_value() &&
           java->dataschema()->view() == "type.googleapis.com/google.protobuf.Struct"sv);
  };

  "sdk-go percent-encodes a non-ASCII source, sdk-java keeps it"_test = [] {
    const auto go = decoded_golden(sdk::go, "unicode"sv);
    const auto java = decoded_golden(sdk::java, "unicode"sv);
    expect(go.has_value() && go->source().view() == "/interop/protobuf/%C3%A9v%C3%A9nement"sv);
    expect(java.has_value() && java->source().view() == "/interop/protobuf/événement"sv);
  };

  "a time with an offset comes back as the same instant in UTC from both SDKs"_test = [] {
    for (const auto producer : {sdk::go, sdk::java}) {
      const auto read = decoded_golden(producer, "time_nanoseconds"sv);
      expect(read.has_value() && read->time().has_value());
      if (read && read->time()) {
        expect(*read->time() == corpus::instant("2026-09-20T10:34:56.123456789Z"));
      }
    }
  };
};

// spec: SWR-PROTO-0009
// spec: SWR-PROTO-0016
const boost::ut::suite<"interop-protobuf-protoc-cases"> protoc_cases = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the cases protoc built are refused with the code of their row"_test = [] {
    for (const auto& [name, code] : std::array{
             std::pair{"attr_unset"sv, ce::errc::invalid_attribute_value},
             std::pair{"spec_version_03"sv, ce::errc::unsupported_spec_version},
             std::pair{"missing_id"sv, ce::errc::missing_required_attribute},
             std::pair{"time_as_string"sv, ce::errc::type_mismatch},
             std::pair{"subject_as_integer"sv, ce::errc::type_mismatch},
             std::pair{"timestamp_negative_nanos"sv, ce::errc::invalid_timestamp},
             std::pair{"timestamp_year_9999"sv, ce::errc::invalid_timestamp},
             std::pair{"timestamp_year_0001"sv, ce::errc::invalid_timestamp},
         }) {
      expect(refused(protoc_case(name), code)) << name;
    }
  };

  "text_data written by protoc is read as text"_test = [] {
    const auto read = decoded(protoc_case("text_data_wire"sv));
    expect(read.has_value() && std::holds_alternative<std::string>(read->data()) &&
           std::get<std::string>(read->data()) == "plain text payload"sv);
  };

  "an unknown field is skipped"_test = [] {
    const auto read = decoded(protoc_case("unknown_field"sv));
    expect(read.has_value() && read->id().view() == "id-unknown"sv);
  };

  "the last of duplicate map keys wins"_test = [] {
    const auto read = decoded(protoc_case("duplicate_keys"sv));
    expect(read.has_value() && read->subject().has_value() &&
           read->subject()->view() == "second"sv);
  };

  "a ce_uri_ref extension is read as a URI-reference"_test = [] {
    const auto read = decoded(protoc_case("uri_ref_extension"sv));
    expect(read.has_value() && read->extension("relref") != nullptr);
    if (read && read->extension("relref") != nullptr) {
      expect(std::holds_alternative<ce::uri_ref>(*read->extension("relref")));
    }
  };

  "an Any of a type nobody registered is an opaque binary payload"_test = [] {
    const auto read = protobuf::decode_detailed(protoc_case("proto_data_unregistered_type"sv));
    expect(read.has_value());
    if (read) {
      expect(std::get<ce::binary>(read->event.data()) ==
             ce::binary{std::byte{1}, std::byte{2}, std::byte{3}});
      expect(read->type_url.has_value() && *read->type_url == "type.example.test/not.Registered"sv);
      expect(read->event.dataschema().has_value() &&
             read->event.dataschema()->view() == "type.example.test/not.Registered"sv);
    }
  };
};

// spec: SWR-PROTO-0024
const boost::ut::suite<"interop-protobuf-batch-golden"> batch_golden = [] {
  using boost::ut::expect;
  using namespace boost::ut::literals;

  "the CloudEventBatch protoc built decodes to its two events"_test = [] {
    const auto read = protobuf::decode_batch(protoc_case("batch_two"sv, "pbbatch"sv));
    expect(read.has_value() && read->size() == 2U);
    if (read && read->size() == 2U) {
      expect((*read)[0].id().view() == "id-b1"sv);
      expect(std::get<std::string>((*read)[0].data()) == "one"sv);
      expect((*read)[1].id().view() == "id-b2"sv);
      expect(std::get<ce::binary>((*read)[1].data()) == ce::binary{std::byte{0}, std::byte{1}});
    }
  };

  "this SDK writes batch bytes that decode to the same two events"_test = [] {
    const auto read = protobuf::decode_batch(protoc_case("batch_two"sv, "pbbatch"sv));
    expect(read.has_value());
    if (read) {
      const auto written = protobuf::encode_batch(*read);
      expect(written.has_value());
      if (written) {
        const auto again = protobuf::decode_batch(*written);
        expect(again.has_value() && *again == *read);
      }
    }
  };
};

int main() {}
