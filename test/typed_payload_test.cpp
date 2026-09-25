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

using namespace ce::literals;

[[nodiscard]] auto minimal(ce::event::options rest = {}) -> ce::event {
  return ce::event{"id-1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
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
  expect(subject.datacontenttype().has_value()) << label;
  if (subject.datacontenttype()) {
    expect(ce::is_json_content_type(subject.datacontenttype()->view())) << label;
  }
  // Stored as a document the codec built, not as an escaped string: the encoded
  // event must carry one document rather than a JSON string holding a document.
  const auto* stored = std::get_if<ce::json_document>(&subject.data());
  expect(stored != nullptr && stored->built_by<C>()) << label;

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
  const ce::event subject =
      minimal({.datacontenttype = "application/json"_mediatype,
               .data = ce::json_text{.raw = R"({"sensor":"s-1","celsius":3})"}});

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

  const ce::event explicit_null =
      minimal({.datacontenttype = "application/json"_mediatype,
               .data = ce::json_text{.raw = R"({"sensor":"s","celsius":1,"note":null})"}});
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
  const ce::event binary = minimal({.data = ce::binary{std::byte{0x00}, std::byte{0x01}}});
  auto from_binary = ce::data_as<reading, C>(binary);
  expect(!from_binary.has_value()) << label;
  if (!from_binary) {
    expect(from_binary.error().code == ce::errc::type_mismatch) << label;
  }

  // Text whose datacontenttype does not claim JSON is refused too, and the same
  // text with a JSON content type is accepted - so the refusal is about the
  // declaration, not the bytes.
  const std::string payload_text{R"({"sensor":"s-1","celsius":1})"};
  const ce::event text =
      minimal({.datacontenttype = "text/plain"_mediatype, .data = payload_text});
  auto from_text = ce::data_as<reading, C>(text);
  expect(!from_text.has_value()) << label;
  if (!from_text) {
    expect(from_text.error().code == ce::errc::type_mismatch) << label;
  }
  const ce::event declared_json =
      minimal({.datacontenttype = "application/json"_mediatype, .data = payload_text});
  expect(ce::data_as<reading, C>(declared_json).has_value()) << label;

  // A member of the wrong JSON type names the member rather than the payload.
  const ce::event wrong =
      minimal({.datacontenttype = "application/json"_mediatype,
               .data = ce::json_text{.raw = R"({"sensor":7,"celsius":1})"}});
  auto mistyped = ce::data_as<reading, C>(wrong);
  expect(!mistyped.has_value()) << label;
  if (!mistyped) {
    expect(mistyped.error().code == ce::errc::type_mismatch) << label;
    expect(mistyped.error().where == "sensor") << label;
  }

  // A JSON document that is not an object cannot be a described struct.
  const ce::event array = minimal(
      {.datacontenttype = "application/json"_mediatype, .data = ce::json_text{.raw = "[1,2,3]"}});
  auto from_array = ce::data_as<reading, C>(array);
  expect(!from_array.has_value()) << label;
  if (!from_array) {
    expect(from_array.error().code == ce::errc::type_mismatch) << label;
  }

  // Unparseable text is a parse error, distinct from a type mismatch.
  const ce::event broken = minimal({.datacontenttype = "application/json"_mediatype,
                                    .data = ce::json_text{.raw = R"({"sensor":)"}});
  auto unparsed = ce::data_as<reading, C>(broken);
  expect(!unparsed.has_value()) << label;
  if (!unparsed) {
    expect(unparsed.error().code == ce::errc::parse_error) << label;
    expect(unparsed.error().code != ce::errc::type_mismatch) << label;
  }

  // An integer too large for the declared field is refused rather than wrapped:
  // a silently truncated value would make the struct disagree with the document.
  const ce::event huge = minimal({.datacontenttype = "application/json"_mediatype,
                                  .data = ce::json_text{.raw = R"({"value":2147483648})"}});
  auto overflowed = ce::data_as<narrow, C>(huge);
  expect(!overflowed.has_value()) << label;
  if (!overflowed) {
    expect(overflowed.error().code == ce::errc::out_of_range) << label;
    expect(overflowed.error().where == "value") << label;
  }
  // The largest value that does fit is still accepted.
  const ce::event edge = minimal({.datacontenttype = "application/json"_mediatype,
                                  .data = ce::json_text{.raw = R"({"value":2147483647})"}});
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

template<class Built, class Read>
void check_payload_from_document(std::string_view label) {
  using namespace boost::ut;

  const auto document = Built::parse(
      R"({"sensor":"s-1","celsius":-7,"calibrated":true,"drift":0.25,"note":"n",)"
      R"("tags":["a"],"labels":{"k":"v"}})");
  expect(document.has_value()) << label << ": parse";
  if (!document) {
    return;
  }
  const ce::event subject =
      minimal({.datacontenttype = "application/json"_mediatype,
               .data = ce::json_document::make<Built>(Built::copy(*document))});

  auto read = ce::data_as<reading, Read>(subject);
  expect(read.has_value()) << label;
  if (read) {
    expect(read->sensor == "s-1" && read->celsius == -7 && read->calibrated) << label;
    expect(read->tags == std::vector<std::string>{"a"}) << label;
    expect(read->labels.size() == 1_ul) << label;
  }
}

// spec: SWR-JSON-0042
const boost::ut::suite<"typed-payload-reads-a-document-from-any-codec"> payload_from_document =
    [] {
      using namespace boost::ut;

      "nlohmann document read with mini_codec"_test = [] {
        check_payload_from_document<nlohmann_codec, mini_codec>("nlohmann -> mini_codec");
      };
      "mini_codec document read with nlohmann"_test = [] {
        check_payload_from_document<mini_codec, nlohmann_codec>("mini_codec -> nlohmann");
      };
      "a document read with its own codec"_test = [] {
        check_payload_from_document<nlohmann_codec, nlohmann_codec>("nlohmann -> nlohmann");
      };
    };

// --- SWR-EXT-0012 -----------------------------------------------------------

template<class Base>
inline constexpr std::string_view counting_identity{};
template<>
inline constexpr std::string_view counting_identity<nlohmann_codec> =
    "io.cloudevents.cpp.test.counting.nlohmann";
template<>
inline constexpr std::string_view counting_identity<mini_codec> =
    "io.cloudevents.cpp.test.counting.mini";

struct call_counts {
  std::size_t parses = 0;
  std::size_t dumps = 0;
};

/// A codec that counts every parse and dump asked of it, so a suite can show
/// that a path reads the DOM it was given rather than going through text. The
/// counts live in a function-local static: Clang 23 reports a static data
/// member of a class template as set but not used even where it is read.
template<class Base>
struct counting_codec : Base {
  static constexpr std::string_view identity = counting_identity<Base>;

  [[nodiscard]] static auto counts() -> call_counts& {
    static call_counts held{};
    return held;
  }
  [[nodiscard]] static auto parse(std::string_view text) -> ce::result<typename Base::value> {
    ++counts().parses;
    return Base::parse(text);
  }
  [[nodiscard]] static auto dump(const typename Base::value& held) -> std::string {
    ++counts().dumps;
    return Base::dump(held);
  }
  static void reset() { counts() = {}; }
};
static_assert(ce::json::json_codec<counting_codec<nlohmann_codec>>);
static_assert(ce::json::json_codec<counting_codec<mini_codec>>);

template<class Base>
void check_same_codec_document_read(std::string_view label) {
  using namespace boost::ut;
  using counted = counting_codec<Base>;

  const reading sent = sample();
  const ce::event subject =
      minimal({.datacontenttype = "application/json"_mediatype,
               .data = ce::json_document::make<counted>(ce::to_json_value<counted>(sent))});

  counted::reset();
  auto read = ce::data_as<reading, counted>(subject);
  expect(read.has_value()) << label;
  if (read) {
    expect(same(*read, sent)) << label;
  }
  expect(counted::counts().parses == 0U) << label << ": the payload was parsed";
  expect(counted::counts().dumps == 0U) << label << ": the payload was serialised";

  // A member of the wrong type is still reported by name on the fast path.
  const auto mistyped_document = Base::parse(R"({"sensor":7,"celsius":1})");
  expect(mistyped_document.has_value()) << label;
  if (mistyped_document) {
    const ce::event wrong =
        minimal({.datacontenttype = "application/json"_mediatype,
                 .data = ce::json_document::make<counted>(Base::copy(*mistyped_document))});
    auto mistyped = ce::data_as<reading, counted>(wrong);
    expect(!mistyped.has_value()) << label;
    if (!mistyped) {
      expect(mistyped.error().code == ce::errc::type_mismatch) << label;
      expect(mistyped.error().where == "sensor") << label;
    }
    const auto array_document = Base::parse("[1]");
    const ce::event array =
        minimal({.datacontenttype = "application/json"_mediatype,
                 .data = ce::json_document::make<counted>(Base::copy(*array_document))});
    auto from_array = ce::data_as<reading, counted>(array);
    expect(!from_array.has_value()) << label;
    if (!from_array) {
      expect(from_array.error().code == ce::errc::type_mismatch) << label;
      expect(from_array.error().where == "data") << label;
    }
  }

  // A document another codec built is converted through text, once.
  const ce::event foreign =
      minimal({.datacontenttype = "application/json"_mediatype,
               .data = ce::json_document::make<Base>(ce::to_json_value<Base>(sent))});
  counted::reset();
  auto converted = ce::data_as<reading, counted>(foreign);
  expect(converted.has_value()) << label;
  if (converted) {
    expect(same(*converted, sent)) << label;
  }
  expect(counted::counts().parses == 1U) << label << ": a foreign document is parsed once";
}

// spec: SWR-EXT-0012
const boost::ut::suite<"data-as-reads-a-same-codec-document"> same_codec_document_read = [] {
  using namespace boost::ut;

  "nlohmann_codec"_test = [] { check_same_codec_document_read<nlohmann_codec>("nlohmann_codec"); };
  "mini_codec"_test = [] { check_same_codec_document_read<mini_codec>("mini_codec"); };
};

// --- SWR-EXT-0011 -----------------------------------------------------------

template<class Base>
void check_set_data_stores_a_document(std::string_view label) {
  using namespace boost::ut;
  using counted = counting_codec<Base>;
  using format = ce::json_format<counted>;

  const reading sent = sample();
  ce::event subject = minimal();
  counted::reset();
  ce::set_data<reading, counted>(subject, sent);
  expect(counted::counts().dumps == 0U) << label << ": set_data serialised the payload";
  expect(counted::counts().parses == 0U) << label;

  const auto* stored = std::get_if<ce::json_document>(&subject.data());
  expect(stored != nullptr) << label << ": set_data stores a json_document";
  const auto* dom = stored != nullptr ? stored->get<counted>() : nullptr;
  expect(dom != nullptr) << label << ": the document is the codec's own";
  const auto expected = ce::to_json_value<counted>(sent);
  expect(dom != nullptr && counted::equal(*dom, expected)) << label;
  expect(subject.datacontenttype().has_value() &&
         subject.datacontenttype()->view() == "application/json")
      << label;

  // The event equals the same payload held as a document, never as text: a
  // json_text payload never equals a json_document one (D-JSON-4).
  const ce::event as_document =
      minimal({.datacontenttype = "application/json"_mediatype,
               .data = ce::json_document::make<counted>(ce::to_json_value<counted>(sent))});
  expect(bool{subject == as_document}) << label;
  const ce::event as_text = minimal({.datacontenttype = "application/json"_mediatype,
                                     .data = ce::json_text{.raw = Base::dump(expected)}});
  expect(bool{subject != as_text}) << label;

  // An encode with the same codec copies the DOM: the only dump is the output.
  counted::reset();
  const auto encoded = format::encode(subject);
  expect(encoded.has_value()) << label;
  expect(counted::counts().parses == 0U) << label << ": encode parsed the payload";
  expect(counted::counts().dumps == 1U) << label << ": only the output document is serialised";

  // Writing again replaces the document.
  reading second = sent;
  second.sensor = "s-2";
  ce::set_data<reading, counted>(subject, second);
  auto read = ce::data_as<reading, counted>(subject);
  expect(read.has_value() && read->sensor == "s-2") << label;
}

// spec: SWR-EXT-0011
const boost::ut::suite<"set-data-stores-a-document"> set_data_stores_a_document = [] {
  using namespace boost::ut;

  "nlohmann_codec"_test = [] {
    check_set_data_stores_a_document<nlohmann_codec>("nlohmann_codec");
  };
  "mini_codec"_test = [] { check_set_data_stores_a_document<mini_codec>("mini_codec"); };
};

// --- SWR-EXT-0007 -----------------------------------------------------------

constexpr std::string_view sample_payload_text =
    R"({"sensor":"s-1","celsius":-7,"calibrated":true,"drift":0.25,"note":"recalibrated",)"
    R"("tags":["outdoor","north"],"labels":{"site":"oslo","rack":"3"}})";

[[nodiscard]] auto event_text(std::string_view members) -> std::string {
  return std::string{R"({"specversion":"1.0","id":"id-1","source":"/spec/test",)"} +
         R"("type":"com.example.thing")" + std::string{members} + "}";
}

[[nodiscard]] auto sample_event_text() -> std::string {
  return event_text(R"(,"datacontenttype":"application/json","data":)" +
                    std::string{sample_payload_text});
}

/// decode_as must fail where decode and data_as fail, with the same code and place.
template<ce::described T, class C>
void check_decode_as_fails_like_two_steps(std::string_view text,
                                          ce::errc expected,
                                          std::string_view label) {
  using namespace boost::ut;

  const auto typed = ce::decode_as<T, C>(text);
  expect(!typed.has_value()) << label << ": " << text;
  if (typed) {
    return;
  }
  expect(typed.error().code == expected) << label << ": " << text;

  const auto event = ce::json_format<C>::decode(text);
  const auto two_steps = event ? ce::data_as<T, C>(*event) : ce::result<T>{};
  const auto& reference = event ? two_steps.error() : event.error();
  expect(!event || !two_steps.has_value()) << label;
  expect(typed.error().code == reference.code) << label << ": " << text;
  expect(typed.error().where == reference.where) << label << ": " << text;
}

template<class Base>
void check_decode_as(std::string_view label) {
  using namespace boost::ut;
  using counted = counting_codec<Base>;
  using format = ce::json_format<counted>;

  const std::string text = sample_event_text();

  // Within the retention limit: one parse, no serialisation, and the event is
  // the one decode returns, holding the codec's document.
  counted::reset();
  const auto typed = ce::decode_as<reading, counted>(text);
  expect(typed.has_value()) << label;
  expect(counted::counts().parses == 1U) << label << ": decode_as parses once";
  expect(counted::counts().dumps == 0U) << label << ": decode_as serialised the payload";
  const auto reference = format::decode(text);
  expect(reference.has_value()) << label;
  if (typed && reference) {
    expect(same(typed->payload, sample())) << label;
    expect(bool{typed->event == *reference}) << label << ": the event is decode's";
    const auto* kept = std::get_if<ce::json_document>(&typed->event.data());
    expect(kept != nullptr && kept->template built_by<counted>()) << label;
  }

  // Above the limit the event keeps the input's own text, and the payload is
  // still read from the parsed member rather than parsed again.
  for (const auto limit : {std::size_t{0}, std::size_t{1}}) {
    const ce::json::decode_options options{.retain_document_up_to = limit};
    counted::reset();
    const auto over = ce::decode_as<reading, counted>(text, options);
    expect(over.has_value()) << label;
    expect(counted::counts().parses == 1U) << label << ": the text path parses once";
    expect(counted::counts().dumps == 0U) << label << ": the text path serialised the payload";
    const auto kept = format::decode(text, options);
    if (over && kept) {
      expect(same(over->payload, sample())) << label;
      expect(bool{over->event == *kept}) << label << ": the event is decode's";
      const auto* own = std::get_if<ce::json_text>(&over->event.data());
      expect(own != nullptr && own->raw == sample_payload_text) << label;
    }
  }

  // A payload that is not JSON of the described shape fails as data_as does.
  check_decode_as_fails_like_two_steps<reading, counted>(
      event_text(""), ce::errc::missing_required_attribute, label);
  check_decode_as_fails_like_two_steps<reading, counted>(
      event_text(R"(,"data_base64":"AAE=")"), ce::errc::type_mismatch, label);
  check_decode_as_fails_like_two_steps<reading, counted>(
      event_text(R"(,"datacontenttype":"text/plain","data":"{\"sensor\":\"s\"}")"),
      ce::errc::type_mismatch,
      label);
  check_decode_as_fails_like_two_steps<reading, counted>(
      event_text(R"(,"datacontenttype":"application/json","data":{"sensor":7})"),
      ce::errc::type_mismatch,
      label);
  check_decode_as_fails_like_two_steps<reading, counted>(
      event_text(R"(,"datacontenttype":"application/json","data":[1])"),
      ce::errc::type_mismatch,
      label);
  check_decode_as_fails_like_two_steps<narrow, counted>(
      event_text(R"(,"data":{"value":2147483648})"), ce::errc::out_of_range, label);

  // A document that is not a valid event fails as decode does.
  check_decode_as_fails_like_two_steps<reading, counted>(
      R"({"specversion":"1.0","source":"/s","type":"t","data":{}})",
      ce::errc::missing_required_attribute,
      label);
  check_decode_as_fails_like_two_steps<reading, counted>(
      R"({"specversion":"1.0",)", ce::errc::parse_error, label);

  // A mistyped member is named, as data_as names it.
  const auto mistyped = ce::decode_as<reading, counted>(event_text(R"(,"data":{"sensor":7})"));
  expect(!mistyped.has_value() && mistyped.error().where == "sensor") << label;

  // A JSON payload whose datacontenttype is absent is still JSON.
  const auto undeclared = ce::decode_as<narrow, counted>(event_text(R"(,"data":{"value":3})"));
  expect(undeclared.has_value() && undeclared->payload.value == 3) << label;
}

// spec: SWR-EXT-0007
const boost::ut::suite<"decode-as-reads-event-and-payload-in-one-parse"> decode_as_suite = [] {
  using namespace boost::ut;

  "nlohmann_codec"_test = [] { check_decode_as<nlohmann_codec>("nlohmann_codec"); };
  "mini_codec"_test = [] { check_decode_as<mini_codec>("mini_codec"); };

  "decoded names the event and the payload"_test = [] {
    const ce::decoded<narrow> built{.event = minimal(), .payload = {.value = 1}};
    expect(built.event.id() == "id-1");
    expect(built.payload.value == 1);
  };
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
