#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <boost/ut.hpp>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>
#include <cloudevents/result.hpp>

#include "mini_codec.hpp"
#include "no_nlohmann_probe.hpp"

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::codec::nlohmann_codec;
using mini_codec = ce::test::mini_codec;

[[nodiscard]] auto minimal() -> ce::event {
  return ce::event{.id = "id-1", .source = ce::uri_ref{"/spec/test"}, .type = "com.example.thing"};
}

struct reading {
  std::string sensor;
  std::int32_t celsius;
  bool calibrated;
  double drift;
  std::optional<std::string> note;
  std::vector<std::string> tags;
  std::map<std::string, std::string> labels;
};

CE_DESCRIBE(reading, sensor, celsius, calibrated, drift, note, tags, labels);

struct narrow {
  std::int32_t value;
};

CE_DESCRIBE(narrow, value);

[[nodiscard]] auto sample() -> reading {
  return reading{
      .sensor = "s-1",
      .celsius = -7,
      .calibrated = true,
      .drift = 0.25,
      .note = "recalibrated",
      .tags = {"outdoor", "north"},
      .labels = {{"site", "oslo"}, {"rack", "3"}},
  };
}

// The probe must be a template, or naming a member that does not exist is a
// hard error rather than an unsatisfied requirement.
template<class E>
concept carries_typed_payload_member =
    requires(E subject, reading value) { subject.set_data(value); };

template<class E>
concept carries_typed_extension_member = requires(E subject, ce::ext::tracing value) {
  subject.set(value);
  subject.template get<ce::ext::tracing>();
};

[[nodiscard]] auto same(const reading& left, const reading& right) -> bool {
  return left.sensor == right.sensor && left.celsius == right.celsius &&
         left.calibrated == right.calibrated && left.drift == right.drift &&
         left.note == right.note && left.tags == right.tags && left.labels == right.labels;
}

// --- SWR-EXT-0005 -----------------------------------------------------------

template<class C>
void check_payload_roundtrip(std::string_view label) {
  using namespace boost::ut;

  ce::event subject = minimal();
  const reading sent = sample();

  ce::set_data<reading, C>(subject, sent);

  // The event states what it carries, so a peer that only reads attributes can
  // tell the payload is JSON.
  expect(subject.datacontenttype.has_value()) << label;
  if (subject.datacontenttype) {
    expect(ce::is_json_content_type(*subject.datacontenttype)) << label;
  }
  // Stored as json_text, not as an escaped string: the encoded event must carry
  // one document rather than a JSON string holding a document.
  expect(std::holds_alternative<ce::json_text>(subject.data)) << label;

  auto read = ce::data_as<reading, C>(subject);
  expect(read.has_value()) << label;
  if (read) {
    expect(same(*read, sent)) << label;
  }

  // Every field shape the describe seam supports survives, not just the scalars.
  if (read) {
    expect(read->tags.size() == 2_ul) << label;
    expect(read->labels.size() == 2_ul) << label;
    expect(read->note.has_value()) << label;
    expect(read->drift == 0.25_d) << label;
    expect(read->celsius == -7) << label;
    expect(read->calibrated) << label;
  }
}

template<class C>
void check_payload_through_the_wire(std::string_view label) {
  using namespace boost::ut;
  using format = ce::json_format<C>;

  ce::event subject = minimal();
  const reading sent = sample();
  ce::set_data<reading, C>(subject, sent);

  auto encoded = format::encode(subject);
  expect(encoded.has_value()) << label;
  if (!encoded) {
    return;
  }
  auto decoded = format::decode(*encoded);
  expect(decoded.has_value()) << label;
  if (!decoded) {
    return;
  }

  auto read = ce::data_as<reading, C>(*decoded);
  expect(read.has_value()) << label;
  if (read) {
    expect(same(*read, sent)) << label;
  }
}

template<class C>
void check_payload_absent_members(std::string_view label) {
  using namespace boost::ut;

  // A document that omits a member leaves the field at its default. An optional
  // that is absent and an optional that is explicitly null mean the same thing.
  ce::event subject = minimal();
  subject.datacontenttype = "application/json";
  subject.data = ce::json_text{.raw = R"({"sensor":"s-1","celsius":3})"};

  auto read = ce::data_as<reading, C>(subject);
  expect(read.has_value()) << label;
  if (read) {
    expect(read->sensor == "s-1") << label;
    expect(read->celsius == 3) << label;
    expect(!read->note.has_value()) << label;
    expect(read->tags.empty()) << label;
    expect(read->labels.empty()) << label;
    expect(!read->calibrated) << label;
  }

  ce::event explicit_null = minimal();
  explicit_null.datacontenttype = "application/json";
  explicit_null.data = ce::json_text{.raw = R"({"sensor":"s","celsius":1,"note":null})"};
  auto null_read = ce::data_as<reading, C>(explicit_null);
  expect(null_read.has_value()) << label;
  if (null_read) {
    expect(!null_read->note.has_value()) << label;
  }
}

template<class C>
void check_payload_failures(std::string_view label) {
  using namespace boost::ut;

  // No payload at all.
  auto empty = ce::data_as<reading, C>(minimal());
  expect(!empty.has_value()) << label;
  if (!empty) {
    expect(empty.error().code == ce::errc::missing_required_attribute) << label;
    expect(empty.error().where == "data") << label;
  }

  // Bytes are refused rather than guessed at.
  ce::event binary = minimal();
  binary.data = ce::binary{std::byte{0x00}, std::byte{0x01}};
  auto from_binary = ce::data_as<reading, C>(binary);
  expect(!from_binary.has_value()) << label;
  if (!from_binary) {
    expect(from_binary.error().code == ce::errc::type_mismatch) << label;
  }

  // Text whose datacontenttype does not claim JSON is refused too, and the same
  // text with a JSON content type is accepted - so the refusal is about the
  // declaration, not the bytes.
  ce::event text = minimal();
  text.datacontenttype = "text/plain";
  text.data = std::string{R"({"sensor":"s-1","celsius":1})"};
  auto from_text = ce::data_as<reading, C>(text);
  expect(!from_text.has_value()) << label;
  if (!from_text) {
    expect(from_text.error().code == ce::errc::type_mismatch) << label;
  }
  text.datacontenttype = "application/json";
  expect(ce::data_as<reading, C>(text).has_value()) << label;

  // A member of the wrong JSON type names the member rather than the payload.
  ce::event wrong = minimal();
  wrong.datacontenttype = "application/json";
  wrong.data = ce::json_text{.raw = R"({"sensor":7,"celsius":1})"};
  auto mistyped = ce::data_as<reading, C>(wrong);
  expect(!mistyped.has_value()) << label;
  if (!mistyped) {
    expect(mistyped.error().code == ce::errc::type_mismatch) << label;
    expect(mistyped.error().where == "sensor") << label;
  }

  // A JSON document that is not an object cannot be a described struct.
  ce::event array = minimal();
  array.datacontenttype = "application/json";
  array.data = ce::json_text{.raw = "[1,2,3]"};
  auto from_array = ce::data_as<reading, C>(array);
  expect(!from_array.has_value()) << label;
  if (!from_array) {
    expect(from_array.error().code == ce::errc::type_mismatch) << label;
  }

  // Unparseable text is a parse error, distinct from a type mismatch.
  ce::event broken = minimal();
  broken.datacontenttype = "application/json";
  broken.data = ce::json_text{.raw = R"({"sensor":)"};
  auto unparsed = ce::data_as<reading, C>(broken);
  expect(!unparsed.has_value()) << label;
  if (!unparsed) {
    expect(unparsed.error().code == ce::errc::parse_error) << label;
    expect(unparsed.error().code != ce::errc::type_mismatch) << label;
  }

  // An integer too large for the declared field is refused rather than wrapped:
  // a silently truncated value would make the struct disagree with the document.
  ce::event huge = minimal();
  huge.datacontenttype = "application/json";
  huge.data = ce::json_text{.raw = R"({"value":2147483648})"};
  auto overflowed = ce::data_as<narrow, C>(huge);
  expect(!overflowed.has_value()) << label;
  if (!overflowed) {
    expect(overflowed.error().code == ce::errc::out_of_range) << label;
    expect(overflowed.error().where == "value") << label;
  }
  // The largest value that does fit is still accepted.
  ce::event edge = minimal();
  edge.datacontenttype = "application/json";
  edge.data = ce::json_text{.raw = R"({"value":2147483647})"};
  auto at_edge = ce::data_as<narrow, C>(edge);
  expect(at_edge.has_value()) << label;
  if (at_edge) {
    expect(at_edge->value == 2147483647) << label;
  }
}

// spec: SWR-EXT-0005
const boost::ut::suite<"typed-payload-roundtrip"> payload_roundtrip = [] {
  using namespace boost::ut;

  "roundtrip nlohmann_codec"_test = [] {
    check_payload_roundtrip<nlohmann_codec>("nlohmann_codec");
  };
  "roundtrip mini_codec"_test = [] { check_payload_roundtrip<mini_codec>("mini_codec"); };
  "through the wire nlohmann_codec"_test = [] {
    check_payload_through_the_wire<nlohmann_codec>("nlohmann_codec");
  };
  "through the wire mini_codec"_test = [] {
    check_payload_through_the_wire<mini_codec>("mini_codec");
  };
  "absent members nlohmann_codec"_test = [] {
    check_payload_absent_members<nlohmann_codec>("nlohmann_codec");
  };
  "absent members mini_codec"_test = [] { check_payload_absent_members<mini_codec>("mini_codec"); };
  "failures nlohmann_codec"_test = [] { check_payload_failures<nlohmann_codec>("nlohmann_codec"); };
  "failures mini_codec"_test = [] { check_payload_failures<mini_codec>("mini_codec"); };
};

// --- SWR-EXT-0006 -----------------------------------------------------------

// spec: SWR-EXT-0006
const boost::ut::suite<"typed-payload-layering"> payload_layering = [] {
  using namespace boost::ut;

  "the accessors compile and work with no nlohmann in the translation unit"_test = [] {
    // no_nlohmann_probe.cpp includes every public SDK header EXCEPT the nlohmann
    // codec, and it fails to compile if any of them pulls nlohmann in. No runtime
    // assertion can show absence; the translation unit is the evidence, and this
    // reads back what it observed.
    const auto observed = ce_no_nlohmann::probe();

    expect(!observed.nlohmann_macro_defined)
        << "a public SDK header pulled nlohmann into a codec-free translation unit";
    expect(observed.typed_payload_round_tripped)
        << "set_data/data_as did not work over a user-supplied codec";
    expect(observed.typed_extension_round_tripped)
        << "the typed extension layer did not work without a codec";
  };

  "the detector is not vacuous"_test = [] {
    // This translation unit DOES include nlohmann, so the same expression that
    // reads false in the probe reads true here. Without this, the assertion above
    // would pass just as well if the macro had been renamed.
    constexpr bool seen_here =
#ifdef NLOHMANN_JSON_VERSION_MAJOR
        true;
#else
        false;
#endif
    expect(seen_here) << "the nlohmann detector no longer detects nlohmann";
  };

  "the accessors are free functions, not members of event"_test = [] {
    // Core may not name a codec, so the typed payload surface cannot be a member
    // of ce::event. Calling them as free functions is what pins that.
    ce::event subject = minimal();
    const reading sent = sample();
    ce::set_data<reading, nlohmann_codec>(subject, sent);
    expect(ce::data_as<reading, nlohmann_codec>(subject).has_value());

    static_assert(!carries_typed_payload_member<ce::event>,
                  "ce::event must not gain a codec-dependent member");
    expect(true);
  };

  "the extension layer stays in core"_test = [] {
    // The mirror of the rule above: get/set ARE members, because an extension
    // attribute needs no codec at all.
    static_assert(carries_typed_extension_member<ce::event>);
    expect(true);
  };
};

}  // namespace

int main() {}
