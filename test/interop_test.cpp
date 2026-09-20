#include <boost/ut.hpp>

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
constexpr std::array documents{"minimal"sv, "full"sv, "extensions"sv, "text_data"sv,
                               "binary_data"sv};

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
      expect(bool{decoded[i].time == decoded[0].time}) << label << ": " << name << " time";
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
    expect(present == 15_ul) << "expected 15 golden documents, found " << present;
  };

  "the producers really did produce different documents"_test = [] {
    // If the Go and Java goldens were copies of ours, agreement would be
    // vacuous. Each names its own source, so the bytes must differ.
    for (const auto name : documents) {
      const std::string go = read_golden("go", name);
      const std::string java = read_golden("java", name);
      const std::string cpp = read_golden("cpp", name);
      expect(go != cpp) << name << ": the Go golden is byte-identical to ours";
      expect(java != cpp) << name << ": the Java golden is byte-identical to ours";
      expect(go != java) << name << ": the Go and Java goldens are byte-identical";
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

int main() {}
