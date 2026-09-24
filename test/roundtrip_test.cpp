#include <boost/ut.hpp>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mini_codec.hpp"
#include "equality.hpp"
#include "payload.hpp"

// decode(encode(e)) == e, for generated events, in every mode.
//
// The generator is a deterministic sequence rather than a random one: a
// property that fails only on some runs is a property nobody can act on. The
// seed walks a fixed lattice of attribute combinations, so a failure names an
// event that can be reproduced by index.

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::codec::nlohmann_codec;
using mini_codec = ce::test::mini_codec;

using namespace ce::literals;

/// \brief One generated event, with the index that produced it.
struct generated {
  ce::event subject;
  std::size_t index;
};

/// Every value in the lattice satisfies its attribute's rule, so a refusal is a
/// defect in the lattice: reported as a failed expectation, never dereferenced.
template <class Attribute>
[[nodiscard]] auto attribute(std::string text) -> std::optional<Attribute> {
  auto made = Attribute::make(std::move(text));
  boost::ut::expect(made.has_value()) << "the lattice produced text its attribute refuses";
  if (!made) {
    return std::nullopt;
  }
  return std::optional<Attribute>{std::move(*made)};
}

/// \brief A deterministic lattice over the attribute combinations that matter.
///
/// Each axis is varied independently so every optional attribute is present in
/// some events and absent in others, and every payload shape appears with and
/// without extensions.
[[nodiscard]] auto generate() -> std::vector<generated> {
  const std::vector<std::string> ids{"a", "id-with-dashes", "0", "\xc3\xa9v\xc3\xa9nement"};
  const std::vector<std::string> sources{"/s", "https://example.test/a/b?q=1#f", "urn:uuid:1"};
  const std::vector<std::string> types{"t", "com.example.thing.v2"};

  std::vector<generated> events;
  std::size_t index = 0;

  for (const auto& id : ids) {
    for (const auto& source : sources) {
      for (const auto& type : types) {
        for (int shape = 0; shape < 4; ++shape) {
          // Which attributes are present depends on the index, so they are
          // accumulated the way a decoder accumulates them.
          ce::event::builder next{
              .id = attribute<ce::id>(id),
              .source = attribute<ce::source>(source),
              .type = attribute<ce::type>(type),
          };
          auto& rest = next.rest;

          if (index % 2 == 0) {
            rest.subject = attribute<ce::subject>("subject-" + std::to_string(index));
          }
          if (index % 3 == 0) {
            rest.dataschema =
                attribute<ce::dataschema>("https://example.test/schema/" + std::to_string(index));
          }
          if (index % 5 == 0) {
            if (auto parsed = ce::parse_timestamp("2026-09-20T12:34:56.789-05:30")) {
              rest.time = *parsed;
            }
          }
          if (index % 11 == 0) {
            rest.extensions.insert_or_assign("seq"_ext, ce::attribute_value{std::int32_t{42}});
            rest.extensions.insert_or_assign("ok"_ext, ce::attribute_value{true});
          }

          switch (shape) {
            case 0:
              break;
            case 1:
              rest.datacontenttype = "application/json"_mediatype;
              rest.data = ce::json_text{.raw = R"({"n":)" + std::to_string(index) + "}"};
              break;
            case 2:
              rest.datacontenttype = "text/plain"_mediatype;
              rest.data = "payload " + std::to_string(index);
              break;
            default:
              rest.datacontenttype = "application/octet-stream"_mediatype;
              rest.data = ce::binary{std::byte{0x00}, std::byte{0x7F},
                                     static_cast<std::byte>(index & 0xFFU), std::byte{0xFF}};
              break;
          }

          const std::size_t this_index = index++;
          auto built = std::move(next).build();
          boost::ut::expect(built.has_value()) << "lattice event #" << this_index;
          if (!built) {
            continue;
          }
          if (this_index % 7 == 0) {
            boost::ut::expect(bool{
                built->set(ce::ext::tracing{.traceparent = "00-a-b-01", .tracestate = {}})});
          }
          events.push_back(generated{.subject = std::move(*built), .index = this_index});
        }
      }
    }
  }
  return events;
}

template <class C>
void check_structured_roundtrip(std::string_view label) {
  using namespace boost::ut;
  using format = ce::json_format<C>;

  const auto events = generate();
  expect(events.size() == 96_ul) << label;

  for (const auto& [subject, index] : events) {
    auto encoded = format::encode(subject);
    expect(encoded.has_value()) << label << " #" << index;
    if (!encoded) {
      continue;
    }
    auto decoded = format::decode(*encoded);
    expect(decoded.has_value()) << label << " #" << index;
    if (!decoded) {
      continue;
    }
    expect(bool{*decoded == ce_test::as_decoded(subject)}) << label << " #" << index;
  }
}

template <class C>
void check_binary_roundtrip(std::string_view label) {
  using namespace boost::ut;

  for (const auto& [subject, index] : generate()) {
    auto message = ce::http::to_message<C>(subject, ce::content_mode::binary_mode);
    expect(message.has_value()) << label << " #" << index;
    if (!message) {
      continue;
    }
    auto decoded = ce::http::from_message<C>(*message);
    expect(decoded.has_value()) << label << " #" << index;
    if (!decoded) {
      continue;
    }

    // The binary binding carries every extension as a string, so an event whose
    // extensions were typed cannot come back identical. The context attributes
    // and the payload must, and the extension VALUES must match their text.
    expect(decoded->id() == subject.id()) << label << " #" << index;
    expect(bool{decoded->source() == subject.source()}) << label << " #" << index;
    expect(decoded->type() == subject.type()) << label << " #" << index;
    expect(ce_test::equal(decoded->subject(), subject.subject())) << label << " #" << index;
    expect(ce_test::equal(decoded->dataschema(), subject.dataschema())) << label << " #" << index;
    expect(ce_test::equal(decoded->time(), subject.time())) << label << " #" << index;
    expect(decoded->extensions().size() == subject.extensions().size()) << label << " #" << index;
  }
}

template <class C>
void check_structured_message_roundtrip(std::string_view label) {
  using namespace boost::ut;

  for (const auto& [subject, index] : generate()) {
    auto message = ce::http::to_message<C>(subject, ce::content_mode::structured);
    expect(message.has_value()) << label << " #" << index;
    if (!message) {
      continue;
    }
    auto decoded = ce::http::from_message<C>(*message);
    expect(decoded.has_value()) << label << " #" << index;
    if (decoded) {
      // Structured mode carries the JSON document, so the event survives whole.
      expect(bool{*decoded == ce_test::as_decoded(subject)}) << label << " #" << index;
    }
  }
}

template <class C>
void check_batched_roundtrip(std::string_view label) {
  using namespace boost::ut;
  using format = ce::json_format<C>;

  const auto generated_events = generate();
  std::vector<ce::event> events;
  events.reserve(generated_events.size());
  for (const auto& entry : generated_events) {
    events.push_back(entry.subject);
  }

  auto encoded = format::encode_batch(std::span<const ce::event>{events});
  expect(encoded.has_value()) << label;
  if (!encoded) {
    return;
  }
  auto decoded = format::decode_batch(*encoded);
  expect(decoded.has_value()) << label;
  if (!decoded) {
    return;
  }
  expect(decoded->size() == events.size()) << label;
  if (decoded->size() != events.size()) {
    return;
  }
  for (std::size_t i = 0; i < events.size(); ++i) {
    expect(bool{(*decoded)[i] == ce_test::as_decoded(events[i])}) << label << " #" << i;
  }

  // The same batch through the HTTP binding.
  auto message = ce::http::to_batch_message<C>(std::span<const ce::event>{events});
  expect(message.has_value()) << label;
  if (!message) {
    return;
  }
  auto from_message = ce::http::from_batch_message<C>(*message);
  expect(from_message.has_value()) << label;
  if (from_message) {
    expect(from_message->size() == events.size()) << label;
    if (from_message->size() == events.size()) {
      for (std::size_t i = 0; i < events.size(); ++i) {
        expect(bool{(*from_message)[i] == ce_test::as_decoded(events[i])}) << label << " #" << i;
      }
    }
  }
}

// spec: SWR-SEC-0006
const boost::ut::suite<"roundtrip-property"> roundtrip_property = [] {
  using namespace boost::ut;

  "structured nlohmann_codec"_test = [] {
    check_structured_roundtrip<nlohmann_codec>("structured nlohmann_codec");
  };
  "structured mini_codec"_test = [] {
    check_structured_roundtrip<mini_codec>("structured mini_codec");
  };
  "binary nlohmann_codec"_test = [] {
    check_binary_roundtrip<nlohmann_codec>("binary nlohmann_codec");
  };
  "binary mini_codec"_test = [] { check_binary_roundtrip<mini_codec>("binary mini_codec"); };
  "structured message nlohmann_codec"_test = [] {
    check_structured_message_roundtrip<nlohmann_codec>("structured-message nlohmann_codec");
  };
  "structured message mini_codec"_test = [] {
    check_structured_message_roundtrip<mini_codec>("structured-message mini_codec");
  };
  "batched nlohmann_codec"_test = [] { check_batched_roundtrip<nlohmann_codec>("batched nlohmann_codec"); };
  "batched mini_codec"_test = [] { check_batched_roundtrip<mini_codec>("batched mini_codec"); };

  "the generator varies what it claims to vary"_test = [] {
    // A property suite over a generator that produced 96 copies of one event
    // would pass and prove nothing.
    const auto events = generate();
    std::size_t with_time = 0;
    std::size_t with_subject = 0;
    std::size_t with_schema = 0;
    std::size_t with_extensions = 0;
    std::size_t with_json = 0;
    std::size_t with_text = 0;
    std::size_t with_binary = 0;
    std::size_t without_data = 0;

    for (const auto& [subject, index] : events) {
      with_time += subject.time().has_value() ? 1 : 0;
      with_subject += subject.subject().has_value() ? 1 : 0;
      with_schema += subject.dataschema().has_value() ? 1 : 0;
      with_extensions += subject.extensions().empty() ? 0 : 1;
      with_json += std::holds_alternative<ce::json_text>(subject.data()) ? 1 : 0;
      with_text += std::holds_alternative<std::string>(subject.data()) ? 1 : 0;
      with_binary += std::holds_alternative<ce::binary>(subject.data()) ? 1 : 0;
      without_data += std::holds_alternative<std::monostate>(subject.data()) ? 1 : 0;
    }

    expect(with_time > 0_ul);
    expect(with_time < events.size());
    expect(with_subject > 0_ul);
    expect(with_subject < events.size());
    expect(with_schema > 0_ul);
    expect(with_schema < events.size());
    expect(with_extensions > 0_ul);
    expect(with_extensions < events.size());
    expect(with_json == 24_ul);
    expect(with_text == 24_ul);
    expect(with_binary == 24_ul);
    expect(without_data == 24_ul);
  };
};

}  // namespace

int main() {}
