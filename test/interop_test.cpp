#include <boost/ut.hpp>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/result.hpp>

#include <array>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <chrono>
#include <variant>
#include <vector>

#include "fixtures_path.hpp"
#include "mini_codec.hpp"

// Golden JSON produced by the Go and Java CloudEvents SDKs, and the documents
// this SDK produced for them to read (SWR-SEC-0005, STK-INTEROP-0001).
//
// There is no reference C++ peer to agree with, so agreement with the
// established SDKs is the only external correctness measure available.
//
// This suite proves one direction: their output decodes here. The other
// direction - our output decodes there - cannot be asserted from C++, and is
// proven by interop/run.sh, which feeds test/fixtures/interop/cpp to both SDKs
// and fails if either rejects a document. The committed generator sources are
// what make that reproducible.

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::codec::nlohmann_codec;
using mini_codec = ce::test::mini_codec;

[[nodiscard]] auto read_golden(std::string_view sdk, std::string_view name) -> std::string {
  std::ifstream in{ce_fixtures::path(std::string{"interop/"} + std::string{sdk} + "/" +
                                     std::string{name} + ".json"),
                   std::ios::binary};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}


/// \brief One HTTP binary-mode message exactly as the Go SDK wrote it.
///
/// Recorded by interop/go, not hand-written: the point is to hold the bytes
/// another SDK actually puts on the wire, which no amount of reading its source
/// can substitute for.
[[nodiscard]] auto read_http_wire(std::string_view sdk, std::string_view name) -> ce::message {
  const std::string raw = [&] {
    std::ifstream in{ce_fixtures::path(std::string{"interop/"} + std::string{sdk} + "/http/" +
                                       std::string{name} + ".json"),
                     std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
  }();

  auto document = nlohmann::json::parse(raw, nullptr, false, false);
  ce::message out;
  if (document.is_discarded()) {
    return out;
  }
  for (const auto& entry : document["headers"].items()) {
    out.header_fields.add(entry.key(), entry.value().get<std::string>());
  }
  const auto encoded = document["body_base64"].get<std::string>();
  if (!encoded.empty()) {
    if (auto decoded = ce::base64_decode(encoded)) {
      out.body = *decoded;
    }
  }
  return out;
}

/// Each name has two recordings of the same Go event: the JSON event format, and
/// the HTTP binary-mode headers. Decoding the headers must reach the event the
/// document describes, or the binding disagrees with the format.
constexpr std::array http_wire_names{
    "minimal"sv,     "full"sv,            "extensions"sv, "text_data"sv,
    "binary_data"sv, "extension_types"sv, "unicode"sv,    "minimal_relative"sv,
};

constexpr std::array producers{"go"sv, "java"sv, "cpp"sv};

/// \brief The payload bytes, whatever representation carried them.
///
/// The JSON format lets a non-JSON payload travel either as a JSON string or as
/// data_base64, and the SDKs do not agree on which. The bytes are what must
/// match; the representation is the producer's choice.
[[nodiscard]] auto payload_bytes(const ce::event& subject) -> std::vector<std::byte> {
  if (const auto* text = std::get_if<std::string>(&subject.data)) {
    std::vector<std::byte> out;
    out.reserve(text->size());
    for (const char character : *text) {
      out.push_back(static_cast<std::byte>(character));
    }
    return out;
  }
  if (const auto* bytes = std::get_if<ce::binary>(&subject.data)) {
    return *bytes;
  }
  if (const auto* json = std::get_if<ce::json_text>(&subject.data)) {
    std::vector<std::byte> out;
    out.reserve(json->raw.size());
    for (const char character : json->raw) {
      out.push_back(static_cast<std::byte>(character));
    }
    return out;
  }
  return {};
}
constexpr std::array documents{
    "minimal"sv,          "full"sv,            "extensions"sv,      "text_data"sv,
    "binary_data"sv,      "time_nanoseconds"sv, "extension_types"sv, "unicode"sv,
    "data_array"sv,       "minimal_relative"sv,
};

/// Only the Go and C++ generators emit a batch; the Java SDK reads ours.
constexpr std::array batch_producers{"go"sv, "cpp"sv};

template <class C>
void check_goldens_decode(std::string_view label) {
  using namespace boost::ut;
  using format = ce::json_format<C>;

  for (const auto sdk : producers) {
    for (const auto name : documents) {
      const std::string golden = read_golden(sdk, name);
      expect(!golden.empty()) << label << ": " << sdk << "/" << name << " is missing";
      if (golden.empty()) {
        continue;
      }

      auto decoded = format::decode(golden);
      expect(decoded.has_value()) << label << ": " << sdk << "/" << name << " did not decode";
      if (!decoded) {
        continue;
      }
      expect(decoded->validate().has_value()) << label << ": " << sdk << "/" << name;
      expect(decoded->specversion == "1.0") << label << ": " << sdk << "/" << name;

      // What decoded must re-encode, and the result must decode to the same
      // event: a golden we can read but not reproduce is not interoperability.
      auto again = format::encode(*decoded);
      expect(again.has_value()) << label << ": " << sdk << "/" << name;
      if (again) {
        auto second = format::decode(*again);
        expect(second.has_value()) << label << ": " << sdk << "/" << name;
        if (second) {
          expect(bool{*second == *decoded}) << label << ": " << sdk << "/" << name;
        }
      }
    }
  }
}

template <class C>
void check_cross_sdk_agreement(std::string_view label) {
  using namespace boost::ut;
  using format = ce::json_format<C>;

  // The three SDKs were asked for the same events, differing only in the source
  // and the payload label, so everything else must agree.
  for (const auto name : documents) {
    std::vector<ce::event> decoded;
    for (const auto sdk : producers) {
      auto one = format::decode(read_golden(sdk, name));
      expect(one.has_value()) << label << ": " << sdk << "/" << name;
      if (one) {
        decoded.push_back(*one);
      }
    }
    expect(decoded.size() == producers.size()) << label << ": " << name;
    if (decoded.size() != producers.size()) {
      continue;
    }

    for (std::size_t i = 1; i < decoded.size(); ++i) {
      expect(decoded[i].id == decoded[0].id) << label << ": " << name << " id";
      expect(decoded[i].type == decoded[0].type) << label << ": " << name << " type";
      expect(bool{decoded[i].subject == decoded[0].subject}) << label << ": " << name << " subject";
      // The INSTANT, not the text. Go normalises an offset to UTC while Java
      // and this SDK keep it, so the same moment has two spellings and the
      // default comparison would report a disagreement that is not one.
      expect(decoded[i].time.has_value() == decoded[0].time.has_value())
          << label << ": " << name << " time presence";
      if (decoded[i].time && decoded[0].time) {
        expect(bool{decoded[i].time->utc == decoded[0].time->utc})
            << label << ": " << name << " time instant";
      }
      expect(bool{decoded[i].dataschema == decoded[0].dataschema})
          << label << ": " << name << " dataschema";
      expect(bool{decoded[i].datacontenttype == decoded[0].datacontenttype})
          << label << ": " << name << " datacontenttype";
      // Not the representation - see the Java divergence pinned below - but
      // whether a payload is present at all must agree.
      const bool left_empty = std::holds_alternative<std::monostate>(decoded[0].data);
      const bool right_empty = std::holds_alternative<std::monostate>(decoded[i].data);
      expect(left_empty == right_empty) << label << ": " << name << " payload presence";
      expect(decoded[i].extensions.size() == decoded[0].extensions.size())
          << label << ": " << name << " extension count";
    }
  }
}

template <class C>
void check_binary_payload_agreement(std::string_view label) {
  using namespace boost::ut;
  using format = ce::json_format<C>;

  // All three encoded the same four bytes as data_base64, so the decoded bytes
  // must be identical. This is the case a base64 disagreement would show up in.
  const ce::binary expected{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0xFF}};
  for (const auto sdk : producers) {
    auto decoded = format::decode(read_golden(sdk, "binary_data"));
    expect(decoded.has_value()) << label << ": " << sdk;
    if (!decoded) {
      continue;
    }
    expect(std::holds_alternative<ce::binary>(decoded->data)) << label << ": " << sdk;
    if (const auto* bytes = std::get_if<ce::binary>(&decoded->data)) {
      expect(bool{*bytes == expected}) << label << ": " << sdk << " payload bytes differ";
    }
  }
}

template <class C>
void check_extension_agreement(std::string_view label) {
  using namespace boost::ut;
  using format = ce::json_format<C>;

  // The typed extension layer must read what the other SDKs wrote, which is the
  // point of tracing surviving a round trip through a foreign producer.
  for (const auto sdk : producers) {
    auto decoded = format::decode(read_golden(sdk, "extensions"));
    expect(decoded.has_value()) << label << ": " << sdk;
    if (!decoded) {
      continue;
    }

    auto tracing = decoded->template get<ce::ext::tracing>();
    expect(tracing.has_value()) << label << ": " << sdk << " tracing";
    if (tracing) {
      expect(tracing->traceparent ==
             "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01")
          << label << ": " << sdk;
    }

    auto partitioning = decoded->template get<ce::ext::partitioning>();
    expect(partitioning.has_value()) << label << ": " << sdk << " partitioning";
    if (partitioning) {
      expect(partitioning->partitionkey == "customer-42") << label << ": " << sdk;
    }

    // sampledrate is an Integer, and each SDK encoded it as a JSON number, so
    // the typed read must give 30 rather than a string.
    auto rate = decoded->template get<ce::ext::sampled_rate>();
    expect(rate.has_value()) << label << ": " << sdk << " sampledrate";
    if (rate) {
      expect(rate->sampledrate == 30) << label << ": " << sdk;
      expect(rate->validate().has_value()) << label << ": " << sdk;
    }
  }
}

template <class C>
void check_payload_bytes_agree(std::string_view label) {
  using namespace boost::ut;
  using format = ce::json_format<C>;

  // text_data and binary_data carry the same bytes from every producer, whether
  // those bytes travelled as a JSON string or as data_base64.
  for (const auto name : {"text_data"sv, "binary_data"sv}) {
    std::vector<std::vector<std::byte>> payloads;
    for (const auto sdk : producers) {
      auto decoded = format::decode(read_golden(sdk, name));
      expect(decoded.has_value()) << label << ": " << sdk << "/" << name;
      if (decoded) {
        payloads.push_back(payload_bytes(*decoded));
      }
    }
    expect(payloads.size() == producers.size()) << label << ": " << name;
    for (std::size_t i = 1; i < payloads.size(); ++i) {
      expect(bool{payloads[i] == payloads[0]})
          << label << ": " << name << " payload bytes differ between producers";
    }
    expect(!payloads.empty() && !payloads[0].empty()) << label << ": " << name;
  }
}

// spec: SWR-SEC-0005
// spec: STK-INTEROP-0001
const boost::ut::suite<"interop-golden-corpus"> interop = [] {
  using namespace boost::ut;

  "the corpus covers three producers and five shapes"_test = [] {
    // A suite over an empty corpus passes and proves nothing.
    std::size_t present = 0;
    for (const auto sdk : producers) {
      for (const auto name : documents) {
        if (!read_golden(sdk, name).empty()) {
          ++present;
        }
      }
    }
    expect(present == 30_ul) << "expected 30 golden documents, found " << present;
  };

  "the producers really did produce different documents"_test = [] {
    // If the goldens were copies of ours, agreement would be vacuous. Every
    // document names its own producer in the source, so ours must differ from
    // both of theirs.
    for (const auto name : documents) {
      const std::string go = read_golden("go", name);
      const std::string java = read_golden("java", name);
      const std::string cpp = read_golden("cpp", name);
      expect(go != cpp) << name << ": the Go golden is byte-identical to ours";
      expect(java != cpp) << name << ": the Java golden is byte-identical to ours";
    }

    // Go and Java differ on nearly everything, but not on all of it, so the
    // check is on the corpus rather than on each document.
    std::size_t differing = 0;
    for (const auto name : documents) {
      if (read_golden("go", name) != read_golden("java", name)) {
        ++differing;
      }
    }
    expect(differing >= documents.size() - 1)
        << "the Go and Java goldens agree too often to be independent";
  };

  "three SDKs emit the same bytes for a minimal event, bar key order"_test = [] {
    // minimal_relative carries only the four required attributes and a source
    // that names no producer, so there is nothing left to disagree about.
    //
    // Go and Java are byte-identical. Ours differs only in key order - the
    // nlohmann codec keeps an object sorted, and JSON does not order members.
    const std::string go = read_golden("go", "minimal_relative");
    const std::string java = read_golden("java", "minimal_relative");
    const std::string cpp = read_golden("cpp", "minimal_relative");

    expect(go == java) << "Go and Java stopped agreeing on the minimal document";
    expect(cpp != go) << "ours now matches theirs byte for byte, which is new";

    // And all three decode to the same event, which is the part that matters.
    using format = ce::json_format<nlohmann_codec>;
    auto a = format::decode(go);
    auto b = format::decode(java);
    auto c = format::decode(cpp);
    expect(a.has_value());
    expect(b.has_value());
    expect(c.has_value());
    if (a && b && c) {
      expect(bool{*a == *b});
      expect(bool{*a == *c});
    }
  };

  "goldens decode nlohmann_codec"_test = [] { check_goldens_decode<nlohmann_codec>("nlohmann_codec"); };
  "goldens decode mini_codec"_test = [] { check_goldens_decode<mini_codec>("mini_codec"); };
  "cross-sdk agreement nlohmann_codec"_test = [] {
    check_cross_sdk_agreement<nlohmann_codec>("nlohmann_codec");
  };
  "cross-sdk agreement mini_codec"_test = [] {
    check_cross_sdk_agreement<mini_codec>("mini_codec");
  };
  "binary payload agreement nlohmann_codec"_test = [] {
    check_binary_payload_agreement<nlohmann_codec>("nlohmann_codec");
  };
  "binary payload agreement mini_codec"_test = [] {
    check_binary_payload_agreement<mini_codec>("mini_codec");
  };
  "extension agreement nlohmann_codec"_test = [] {
    check_extension_agreement<nlohmann_codec>("nlohmann_codec");
  };
  "extension agreement mini_codec"_test = [] {
    check_extension_agreement<mini_codec>("mini_codec");
  };

  "payload bytes agree nlohmann_codec"_test = [] {
    check_payload_bytes_agree<nlohmann_codec>("nlohmann_codec");
  };
  "payload bytes agree mini_codec"_test = [] {
    check_payload_bytes_agree<mini_codec>("mini_codec");
  };

  "every producer's batch decodes here"_test = [] {
    using format = ce::json_format<nlohmann_codec>;
    for (const auto sdk : batch_producers) {
      const std::string golden = read_golden(sdk, "batch");
      expect(!golden.empty()) << sdk << ": batch is missing";
      auto events = format::decode_batch(golden);
      expect(events.has_value()) << sdk << ": batch did not decode";
      if (events) {
        expect(events->size() == 3_ul) << sdk << ": expected 3 events";
        for (const auto& subject : *events) {
          expect(subject.validate().has_value()) << sdk;
        }
      }
    }
  };

  "the SDKs disagree about the offset in a timestamp"_test = [] {
    using format = ce::json_format<nlohmann_codec>;

    // All three were given 2026-09-20T12:34:56.123456789+02:00. Go marshals a
    // time.Time, which carries no offset, so it emits the UTC spelling. Java
    // and this SDK keep what they were given.
    //
    // The instant is identical, and that is what interoperability means here.
    // But ce::timestamp compares its TEXT, because SWR-CORE-0009 requires a
    // byte-for-byte round trip - re-emitting an event must not alter a value a
    // peer may have signed. The consequence is that the same moment from two
    // producers is not operator==, and a consumer comparing events across SDKs
    // has to compare .utc.
    auto from_go = format::decode(read_golden("go", "time_nanoseconds"));
    auto from_java = format::decode(read_golden("java", "time_nanoseconds"));
    auto from_cpp = format::decode(read_golden("cpp", "time_nanoseconds"));
    expect(from_go.has_value());
    expect(from_java.has_value());
    expect(from_cpp.has_value());
    if (!from_go || !from_java || !from_cpp) {
      return;
    }
    expect(from_go->time.has_value());
    expect(from_java->time.has_value());
    if (!from_go->time || !from_java->time || !from_cpp->time) {
      return;
    }

    expect(bool{from_go->time->utc == from_java->time->utc}) << "the instants must agree";
    expect(bool{from_cpp->time->utc == from_java->time->utc}) << "the instants must agree";

    // Go dropped the offset; Java and this SDK kept it.
    expect(from_go->time->offset == std::chrono::minutes{0});
    expect(from_java->time->offset == std::chrono::minutes{120});
    expect(from_cpp->time->offset == std::chrono::minutes{120});

    // Nanosecond precision survives everywhere.
    expect(from_go->time->fractional_digits.count() == 9_u);
    expect(from_java->time->fractional_digits.count() == 9_u);

    // And the pinned consequence: same instant, not equal.
    expect(!(*from_go->time == *from_java->time))
        << "timestamp equality is textual; if this changes, say so in the release notes";
  };

  "non-ASCII in a source is written differently and read the same"_test = [] {
    using format = ce::json_format<nlohmann_codec>;

    // Go and Java percent-encode a non-ASCII source, because each holds it in a
    // URI type. This SDK carries the text it was given, because SPEC 5.1 checks
    // source for non-emptiness only and does not parse URIs.
    //
    // Both forms are accepted by all three, which is what matters. A producer
    // that needs RFC 3986 escaping has to do it before handing the value over.
    auto from_go = format::decode(read_golden("go", "unicode"));
    auto from_cpp = format::decode(read_golden("cpp", "unicode"));
    expect(from_go.has_value());
    expect(from_cpp.has_value());
    if (!from_go || !from_cpp) {
      return;
    }
    expect(from_go->source.view().find('%') != std::string_view::npos)
        << "Go stopped percent-encoding the source";
    expect(from_cpp->source.view().find('%') == std::string_view::npos)
        << "this SDK started escaping the source";

    // The subject is not a URI, so every producer carries it verbatim.
    expect(bool{from_go->subject == from_cpp->subject});
  };

  "the SDKs disagree about how to carry a non-JSON payload"_test = [] {
    using format = ce::json_format<nlohmann_codec>;

    // A measured property of the ecosystem, pinned so it is a known fact rather
    // than a surprise. Given the same text/plain payload:
    //
    //   Go and this SDK write a JSON string under "data"
    //   Java writes "data_base64", because its API took a byte[]
    //
    // Both are permitted by the JSON format. A consumer that assumes either one
    // will break against the other, which is why data_as and the data variant
    // both exist.
    auto from_go = format::decode(read_golden("go", "text_data"));
    auto from_java = format::decode(read_golden("java", "text_data"));
    auto from_cpp = format::decode(read_golden("cpp", "text_data"));

    expect(from_go.has_value());
    expect(from_java.has_value());
    expect(from_cpp.has_value());
    if (!from_go || !from_java || !from_cpp) {
      return;
    }

    expect(std::holds_alternative<std::string>(from_go->data)) << "Go changed representation";
    expect(std::holds_alternative<std::string>(from_cpp->data)) << "this SDK changed";
    expect(std::holds_alternative<ce::binary>(from_java->data)) << "Java changed representation";

    // And yet the bytes are the same, which is what interoperability means here.
    expect(bool{payload_bytes(*from_go) == payload_bytes(*from_java)});
    expect(bool{payload_bytes(*from_cpp) == payload_bytes(*from_java)});
  };
};

}  // namespace


// spec: SWR-HTTP-0016
const boost::ut::suite<"interop-http-binary-mode"> interop_http_binary_mode = [] {
  using namespace boost::ut;

  "go binary-mode headers decode to the documented event"_test = [] {
    for (const auto name : http_wire_names) {
      const ce::message wire = read_http_wire("go", name);
      expect(!wire.header_fields.empty()) << name;

      auto from_headers =
          ce::http::from_message<nlohmann_codec, ce::http::literal_values>(wire);
      expect(bool{from_headers}) << name << " headers did not decode";
      if (!from_headers) {
        continue;
      }
      auto from_document = ce::json_format<nlohmann_codec>::decode(read_golden("go", name));
      expect(bool{from_document}) << name;
      if (!from_document) {
        continue;
      }

      expect(from_headers->id == from_document->id) << name;
      expect(bool{from_headers->source == from_document->source}) << name;
      expect(from_headers->type == from_document->type) << name;
      expect(bool{from_headers->subject == from_document->subject}) << name;
      expect(bool{from_headers->time == from_document->time}) << name;
      expect(bool{from_headers->dataschema == from_document->dataschema}) << name;
      expect(bool{payload_bytes(*from_headers) == payload_bytes(*from_document)}) << name;
    }
  };

  // The recorded proof of the incompatibility D-HTTP-1 documents. Go wrote
  // `ce-subject: 100% of 50%OFF`; the conformant reader must refuse it as a
  // malformed escape, and the opt-in policy must take it.
  "a literal percent from go needs the opt-in policy"_test = [] {
    const ce::message wire = read_http_wire("go", "percent_in_subject");
    expect(!wire.header_fields.empty());

    auto conformant = ce::http::from_message<nlohmann_codec>(wire);
    expect(!conformant) << "the conformant reader accepted an unescaped percent";
    if (!conformant) {
      expect(conformant.error().code == ce::errc::parse_error);
    }

    auto compatible = ce::http::from_message<nlohmann_codec, ce::http::literal_values>(wire);
    expect(bool{compatible});
    if (compatible) {
      expect(bool{compatible->subject == std::optional<std::string>{"100% of 50%OFF"}});
      const auto* pct = compatible->extension("pct");
      expect(pct != nullptr);
      if (pct != nullptr) {
        // Not decoded to "a b": naming the policy chooses Go's reading.
        expect(bool{*pct == ce::attribute_value{std::string{"a%20b"}}});
      }
    }
  };
};


// spec: SWR-SEC-0005
const boost::ut::suite<"interop-behaviour-audit"> interop_behaviour_audit = [] {
  using namespace boost::ut;

  // interop/audit/main.go ran these same cases through the Go SDK and its
  // answers are committed beside the goldens. These assert THIS SDK's answer,
  // so a change here is caught even where the two SDKs legitimately differ.
  // interop/audit/README.md has the comparison and who is right on each.

  "the recorded Go behaviour is present and complete"_test = [] {
    std::ifstream in{ce_fixtures::path("interop/go/behaviour.json"), std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    auto document = nlohmann::json::parse(buffer.str(), nullptr, false, false);
    expect(!document.is_discarded());
    if (!document.is_discarded()) {
      expect(document.size() == 21U) << "the audit recorded " << document.size() << " cases";
    }
  };

  "a non-UTC offset survives a write here, unlike Go"_test = [] {
    ce::event subject{.id = "1", .source = "/probe", .type = "com.example.probe"};
    subject.time = *ce::parse_timestamp("2026-09-20T12:34:56.123456789+02:00");

    auto written = ce::http::to_message<nlohmann_codec>(subject, ce::content_mode::binary_mode);
    expect(bool{written});
    if (written) {
      const std::string* time = written->header_fields.find("ce-time");
      expect(time != nullptr);
      if (time != nullptr) {
        // Go writes 2026-09-20T10:34:56.123456789Z for this event. Same instant,
        // different text, so an event that round-trips through Go no longer
        // compares equal to the one that set out.
        expect(*time == "2026-09-20T12:34:56.123456789+02:00"sv) << *time;
      }
    }
  };

  "a lowercase RFC 3339 time is accepted here, unlike Go"_test = [] {
    // RFC 3339 section 5.6: parsers SHOULD accept lower case t and z. Go rejects
    // this input, so a third party emitting it is read here and refused there.
    ce::message request;
    request.header_fields.set("ce-specversion", "1.0");
    request.header_fields.set("ce-id", "1");
    request.header_fields.set("ce-source", "/s");
    request.header_fields.set("ce-type", "t");
    request.header_fields.set("ce-time", "2026-09-20t12:34:56z");

    auto decoded = ce::http::from_message<nlohmann_codec>(request);
    expect(bool{decoded});
    if (decoded) {
      expect(decoded->time.has_value());
    }
  };

  "an empty required or optional attribute is refused here, unlike Go"_test = [] {
    // The core spec requires a present attribute to be non-empty. Go accepts
    // both of these and this SDK refuses them, which is a difference a caller
    // meets as a rejected message rather than as corrupted data.
    const auto with = [](std::string_view name, std::string_view value) {
      ce::message request;
      request.header_fields.set("ce-specversion", "1.0");
      request.header_fields.set("ce-id", "1");
      request.header_fields.set("ce-source", "/s");
      request.header_fields.set("ce-type", "t");
      request.header_fields.set(std::string{name}, std::string{value});
      return request;
    };

    auto empty_subject = ce::http::from_message<nlohmann_codec>(with("ce-subject", ""));
    expect(!empty_subject);
    if (!empty_subject) {
      expect(empty_subject.error().code == ce::errc::invalid_attribute_value);
    }

    auto empty_type = ce::http::from_message<nlohmann_codec>(with("ce-type", ""));
    expect(!empty_type);

    // And writing one is refused too, where Go silently drops the attribute.
    ce::event subject{.id = "1", .source = "/probe", .type = "com.example.probe"};
    subject.subject = "";
    expect(!ce::http::to_message<nlohmann_codec>(subject, ce::content_mode::binary_mode));
  };

  "a specversion this SDK does not implement is refused"_test = [] {
    ce::message request;
    request.header_fields.set("ce-specversion", "0.3");
    request.header_fields.set("ce-id", "1");
    request.header_fields.set("ce-source", "/s");
    request.header_fields.set("ce-type", "t");

    // Go accepts this because it implements 0.3. This SDK claims 1.0 only, so
    // refusing is the honest answer rather than a divergence.
    auto decoded = ce::http::from_message<nlohmann_codec>(request);
    expect(!decoded);
    if (!decoded) {
      expect(decoded.error().code == ce::errc::unsupported_spec_version);
    }
  };
};

int main() {}
