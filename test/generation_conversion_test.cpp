// The v3 <-> v4 event conversions (CR-0004 stage 2, ADR-0012). The header comes
// first, ahead of every other include, so that this suite also shows it is
// self-contained.
// clang-format off
#include <cloudevents/v3_conversion.hpp>
// clang-format on

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <boost/ut.hpp>

#include "equality.hpp"
#include "mini_codec.hpp"

namespace {

using namespace std::string_view_literals;
using namespace ce::literals;
using mini_codec = ce::test::mini_codec;

/// A codec that counts what a conversion must never do to a document: copy the
/// DOM, serialise it, or parse text into one. It is the mini codec in every other
/// respect, so the DOM is the same type and the shared model can hold it.
struct counting_codec : mini_codec {
  static constexpr std::string_view identity = "io.cloudevents.cpp.test.counting";

  static inline std::atomic<std::size_t> copies{0};
  static inline std::atomic<std::size_t> dumps{0};
  static inline std::atomic<std::size_t> parses{0};

  [[nodiscard]] static auto copy(const value& held) -> value {
    ++copies;
    return mini_codec::copy(held);
  }
  [[nodiscard]] static auto dump(const value& subject) -> std::string {
    ++dumps;
    return mini_codec::dump(subject);
  }
  [[nodiscard]] static auto parse(std::string_view text) -> ce::result<value> {
    ++parses;
    return mini_codec::parse(text);
  }
  [[nodiscard]] static auto operations() -> std::size_t { return copies + dumps + parses; }
};
static_assert(ce::json::json_codec<counting_codec>);

/// A second codec, so a document's identity can be told apart after a conversion.
struct other_codec : mini_codec {
  static constexpr std::string_view identity = "io.cloudevents.cpp.test.other";
};
static_assert(ce::json::json_codec<other_codec>);

/// Long enough that no small-string optimisation can hide a copy: a moved
/// string keeps its heap buffer, a copied one gets a new one.
constexpr auto heap_text =
    "a payload long enough to live on the heap rather than inside the string"sv;

/// The five alternatives of data_t, in declaration order, in both generations.
constexpr std::size_t no_payload = 0;
constexpr std::size_t text_payload = 1;
constexpr std::size_t binary_payload = 2;
constexpr std::size_t json_text_payload = 3;
constexpr std::size_t document_payload = 4;
constexpr std::size_t payload_alternatives = 5;

template<class Event>
using data_of = std::remove_cvref_t<decltype(std::declval<const Event&>().data())>;

static_assert(std::variant_size_v<data_of<ce::v3::event>> == payload_alternatives);
static_assert(std::variant_size_v<data_of<ce::v4::event>> == payload_alternatives);

template<class Codec, class Document>
[[nodiscard]] auto document_from(std::string_view text) -> Document {
  auto parsed = Codec::parse(text);
  boost::ut::expect(parsed.has_value()) << "parse " << text;
  return Document::template make<Codec>(parsed ? std::move(*parsed) : Codec::make_null());
}

template<class Event, class Codec = mini_codec>
[[nodiscard]] auto payload_at(std::size_t alternative) -> data_of<Event> {
  using data_type = data_of<Event>;
  using document = std::variant_alternative_t<document_payload, data_type>;
  switch (alternative) {
    case text_payload:
      return data_type{std::string{heap_text}};
    case binary_payload:
      return data_type{ce::binary{std::byte{1}, std::byte{2}, std::byte{3}}};
    case json_text_payload:
      return data_type{ce::json_text{R"({ "kept" : "as written" })"}};
    case document_payload:
      return data_type{document_from<Codec, document>(R"({"n":[1,2,{"k":null}]})"sv)};
    default:
      return data_type{};
  }
}

/// An event with every optional attribute set, so that a conversion that forgot
/// one cannot pass.
template<class Event, class Codec = mini_codec>
[[nodiscard]] auto sample(std::size_t alternative) -> Event {
  return Event{
      "42"_id,
      "/conversion/source"_source,
      "com.example.converted"_type,
      typename Event::options{
          .datacontenttype = "application/json"_mediatype,
          .dataschema = "https://example.com/schema"_dataschema,
          .subject = "subject-1"_subject,
          .time = *ce::parse_timestamp("2024-05-06T07:08:09.123Z"sv),
          .extensions = {{"tenant"_ext, ce::attribute_value{std::string{"acme"}}},
                         {"retries"_ext, ce::attribute_value{std::int32_t{3}}}},
          .data = payload_at<Event, Codec>(alternative),
      },
  };
}

template<class Left, class Right>
[[nodiscard]] auto same_context(const Left& left, const Right& right) -> bool {
  return ce_test::equal(left.id(), right.id()) && ce_test::equal(left.source(), right.source()) &&
         ce_test::equal(left.type(), right.type()) &&
         ce_test::equal(left.datacontenttype(), right.datacontenttype()) &&
         ce_test::equal(left.dataschema(), right.dataschema()) &&
         ce_test::equal(left.subject(), right.subject()) &&
         ce_test::equal(left.time(), right.time()) &&
         ce_test::equal(left.extensions(), right.extensions());
}

/// The payloads of two events agree: the same alternative, the same value, and
/// for a document the same JSON and, for the mini codec, the very same DOM.
template<class Left, class Right>
[[nodiscard]] auto same_payload(const Left& left, const Right& right) -> bool {
  if (left.data().index() != right.data().index()) {
    return false;
  }
  return std::visit(
      [&right](const auto& held) {
        using alternative = std::remove_cvref_t<decltype(held)>;
        if constexpr (requires { held.dump(); }) {
          const auto& other = std::get<document_payload>(right.data());
          return held.dump() == other.dump() &&
                 held.template get<mini_codec>() == other.template get<mini_codec>();
        } else {
          return ce_test::equal(held, std::get<alternative>(right.data()));
        }
      },
      left.data());
}

template<class Left, class Right>
[[nodiscard]] auto same_event(const Left& left, const Right& right) -> bool {
  return same_context(left, right) && same_payload(left, right);
}

template<class Event>
[[nodiscard]] auto heap_address_of(const Event& event) -> const void* {
  if (const auto* text = std::get_if<std::string>(&event.data())) {
    return text->data();
  }
  if (const auto* bytes = std::get_if<ce::binary>(&event.data())) {
    return bytes->data();
  }
  return nullptr;
}

template<class Event>
[[nodiscard]] auto document_of(const Event& event) -> const auto& {
  return std::get<document_payload>(event.data());
}

/// A v4 document that has been moved from.
[[nodiscard]] auto moved_from_v4() -> ce::v4::json_document {
  auto source = document_from<mini_codec, ce::v4::json_document>(R"({"gone":true})"sv);
  const auto taken = std::move(source);
  static_cast<void>(taken);
  return source;  // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
}

[[nodiscard]] auto v4_event_with(ce::v4::json_document document) -> ce::v4::event {
  return ce::v4::event{"7"_id, "/s"_source, "t"_type, {.data = std::move(document)}};
}

// spec: SWR-CORE-0037
const boost::ut::suite<"from-v3-keeps-the-event"> from_v3_keeps_the_event = [] {
  using namespace boost::ut;

  "from_v3 returns the event itself, not a result"_test = [] {
    static_assert(std::is_same_v<decltype(ce::v4::from_v3(std::declval<const ce::v3::event&>())),
                                 ce::v4::event>);
    static_assert(
        std::is_same_v<decltype(ce::v4::from_v3(std::declval<ce::v3::event&&>())), ce::v4::event>);
    expect(true);
  };

  "from_v3 of a const reference keeps every attribute and every payload alternative"_test =
      [](const std::size_t alternative) {
        const auto source = sample<ce::v3::event>(alternative);
        const auto converted = ce::v4::from_v3(source);
        expect(same_event(source, converted)) << "alternative " << alternative;
        expect(same_payload(source, converted));
        expect(ce::v4::to_v3(converted) == source) << "and back again, equal";
      } |
      std::vector<std::size_t>{
          no_payload, text_payload, binary_payload, json_text_payload, document_payload};

  "from_v3 of a const reference leaves the source whole"_test = [] {
    const auto source = sample<ce::v3::event>(text_payload);
    const auto before = sample<ce::v3::event>(text_payload);  // built apart: the oracle
    const auto converted = ce::v4::from_v3(source);
    expect(source == before);
    expect(heap_address_of(converted) != heap_address_of(source)) << "text is copied";
  };

  "from_v3 of an rvalue keeps every attribute and every payload alternative"_test =
      [](const std::size_t alternative) {
        auto source = sample<ce::v3::event>(alternative);
        const auto oracle = source;
        const auto converted = ce::v4::from_v3(std::move(source));
        expect(same_event(oracle, converted)) << "alternative " << alternative;
      } |
      std::vector<std::size_t>{
          no_payload, text_payload, binary_payload, json_text_payload, document_payload};

  "from_v3 of an rvalue moves the text and the bytes instead of copying them"_test =
      [](const std::size_t alternative) {
        auto source = sample<ce::v3::event>(alternative);
        const auto* const buffer = heap_address_of(source);
        expect(buffer != nullptr);
        const auto converted = ce::v4::from_v3(std::move(source));
        expect(heap_address_of(converted) == buffer) << "the heap buffer changed owner";
      } |
      std::vector<std::size_t>{text_payload, binary_payload};

  "an rvalue conversion leaves a v3 document usable, as v3 moves copy"_test = [] {
    auto source = sample<ce::v3::event, counting_codec>(document_payload);
    const auto* const dom = document_of(source).get<counting_codec>();
    const auto converted = ce::v4::from_v3(std::move(source));
    expect(document_of(converted).get<counting_codec>() == dom);
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(document_of(source).get<counting_codec>() == dom)
        << "ce::v3 promises a moved-from document keeps its DOM (SWR-CORE-0036)";
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  };
};

// spec: SWR-CORE-0038
const boost::ut::suite<"to-v3-keeps-the-event"> to_v3_keeps_the_event = [] {
  using namespace boost::ut;

  "to_v3 returns the event itself, not a result"_test = [] {
    static_assert(std::is_same_v<decltype(ce::v4::to_v3(std::declval<const ce::v4::event&>())),
                                 ce::v3::event>);
    static_assert(
        std::is_same_v<decltype(ce::v4::to_v3(std::declval<ce::v4::event&&>())), ce::v3::event>);
    expect(true);
  };

  "to_v3 of a const reference keeps every attribute and every payload alternative"_test =
      [](const std::size_t alternative) {
        const auto source = sample<ce::v4::event>(alternative);
        const auto converted = ce::v4::to_v3(source);
        expect(same_event(source, converted)) << "alternative " << alternative;
        expect(ce::v4::from_v3(converted) == source) << "and back again, equal";
      } |
      std::vector<std::size_t>{
          no_payload, text_payload, binary_payload, json_text_payload, document_payload};

  "to_v3 of a const reference leaves the source whole"_test = [] {
    const auto source = sample<ce::v4::event>(binary_payload);
    const auto before = sample<ce::v4::event>(binary_payload);
    const auto converted = ce::v4::to_v3(source);
    expect(source == before);
    expect(heap_address_of(converted) != heap_address_of(source)) << "bytes are copied";
  };

  "to_v3 of an rvalue keeps every attribute and every payload alternative"_test =
      [](const std::size_t alternative) {
        auto source = sample<ce::v4::event>(alternative);
        const auto oracle = source;
        const auto converted = ce::v4::to_v3(std::move(source));
        expect(same_event(oracle, converted)) << "alternative " << alternative;
      } |
      std::vector<std::size_t>{
          no_payload, text_payload, binary_payload, json_text_payload, document_payload};

  "to_v3 of an rvalue moves the text and the bytes instead of copying them"_test =
      [](const std::size_t alternative) {
        auto source = sample<ce::v4::event>(alternative);
        const auto* const buffer = heap_address_of(source);
        expect(buffer != nullptr);
        const auto converted = ce::v4::to_v3(std::move(source));
        expect(heap_address_of(converted) == buffer) << "the heap buffer changed owner";
      } |
      std::vector<std::size_t>{text_payload, binary_payload};

  "to_v3 of an rvalue leaves a moved-from v4 document behind"_test = [] {
    auto source = sample<ce::v4::event>(document_payload);
    const auto oracle = source;
    const auto converted = ce::v4::to_v3(std::move(source));
    expect(same_event(oracle, converted));
    // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(document_of(source).dump() == "null"sv) << "the v4 move leaves SWR-CORE-0035's state";
    expect(document_of(source).get<mini_codec>() == nullptr);
    // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)
  };
};

// spec: SWR-CORE-0039
const boost::ut::suite<"conversion-shares-the-document"> conversion_shares_the_document = [] {
  using namespace boost::ut;

  "a conversion neither copies, serialises nor parses a DOM"_test = [] {
    const auto v3_source = sample<ce::v3::event, counting_codec>(document_payload);
    const auto v4_source = sample<ce::v4::event, counting_codec>(document_payload);
    const auto before = counting_codec::operations();

    const auto up_by_reference = ce::v4::from_v3(v3_source);
    auto movable_v3 = v3_source;
    const auto up_by_move = ce::v4::from_v3(std::move(movable_v3));
    const auto down_by_reference = ce::v4::to_v3(v4_source);
    auto movable_v4 = v4_source;
    const auto down_by_move = ce::v4::to_v3(std::move(movable_v4));

    expect(counting_codec::operations() == before)
        << "copies " << counting_codec::copies.load() << ", dumps " << counting_codec::dumps.load()
        << ", parses " << counting_codec::parses.load();
    static_cast<void>(up_by_reference);
    static_cast<void>(up_by_move);
    static_cast<void>(down_by_reference);
    static_cast<void>(down_by_move);
  };

  "both generations' documents yield the same DOM"_test = [] {
    const auto v3_source = sample<ce::v3::event, counting_codec>(document_payload);
    const auto* const dom = document_of(v3_source).get<counting_codec>();
    expect(dom != nullptr);

    const auto up = ce::v4::from_v3(v3_source);
    expect(document_of(up).get<counting_codec>() == dom);

    const auto back = ce::v4::to_v3(up);
    expect(document_of(back).get<counting_codec>() == dom);

    auto movable = up;
    const auto moved_down = ce::v4::to_v3(std::move(movable));
    expect(document_of(moved_down).get<counting_codec>() == dom);
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(document_of(movable).get<counting_codec>() == nullptr) << "the v4 source gave it up";
  };

  "a document keeps the codec that built it across the conversion"_test = [] {
    const auto source = sample<ce::v3::event, other_codec>(document_payload);
    const auto converted = ce::v4::from_v3(source);
    const auto& document = document_of(converted);
    expect(document.built_by<other_codec>());
    expect(!document.built_by<mini_codec>());
    expect(!document.built_by<counting_codec>());
    expect(document.get<other_codec>() != nullptr);
    expect(document.get<mini_codec>() == nullptr);

    const auto returned = ce::v4::to_v3(converted);
    const auto& there_and_back = document_of(returned);
    expect(there_and_back.built_by<other_codec>());
    expect(there_and_back.get<other_codec>() == document.get<other_codec>());
    expect(there_and_back.get<mini_codec>() == nullptr);
  };

  "a conversion keeps a json_text and a document apart"_test = [] {
    const auto text = sample<ce::v3::event>(json_text_payload);
    const auto document = sample<ce::v3::event>(document_payload);
    expect(std::holds_alternative<ce::json_text>(ce::v4::from_v3(text).data()));
    expect(std::holds_alternative<ce::v4::json_document>(ce::v4::from_v3(document).data()));
  };
};

// spec: SWR-CORE-0040
const boost::ut::suite<"to-v3-of-a-moved-from-document"> to_v3_of_a_moved_from_document = [] {
  using namespace boost::ut;

  "the v3 document shares the moved-from model and holds no DOM"_test = [] {
    const auto converted = ce::v4::to_v3(v4_event_with(moved_from_v4()));
    const auto* const held = std::get_if<ce::v3::json_document>(&converted.data());
    expect(fatal(held != nullptr)) << "the payload stays a document";
    expect(held->dump() == "null"sv);
    expect(held->get<mini_codec>() == nullptr);
    expect(held->get<counting_codec>() == nullptr);
    expect(held->get<other_codec>() == nullptr);
    expect(!held->built_by<mini_codec>());
  };

  "the rvalue overload gives the same document"_test = [] {
    const auto converted = ce::v4::to_v3(v4_event_with(moved_from_v4()));
    const auto& held = std::get<ce::v3::json_document>(converted.data());
    expect(held.dump() == "null"sv);
    expect(held.get<mini_codec>() == nullptr);
  };

  "a moved-from document converts back to a moved-from v4 document"_test = [] {
    const auto v3_event = ce::v4::to_v3(v4_event_with(moved_from_v4()));
    const auto back = ce::v4::from_v3(v3_event);
    const auto& held = document_of(back);
    expect(held.dump() == "null"sv);
    expect(held.get<mini_codec>() == nullptr);
    expect(held == moved_from_v4()) << "it is the moved-from model again";
    expect(back == v4_event_with(moved_from_v4()));
  };

  // A conversion is the only way to reach a moved-from ce::v3 document. These
  // pin its comparison in both argument orders, so that the result cannot
  // depend on which side the moved-from document stands.
  "two converted moved-from documents are equal in either order"_test = [] {
    const auto first = ce::v4::to_v3(v4_event_with(moved_from_v4()));
    const auto second = ce::v4::to_v3(v4_event_with(moved_from_v4()));
    const auto& left = std::get<ce::v3::json_document>(first.data());
    const auto& right = std::get<ce::v3::json_document>(second.data());
    expect(left == right);
    expect(right == left);
    expect(first == second);
  };

  "a converted moved-from document differs from a JSON null in either order"_test = [] {
    const auto moved = ce::v4::to_v3(v4_event_with(moved_from_v4()));
    const auto& gone = std::get<ce::v3::json_document>(moved.data());
    for (const auto& json_null : {document_from<mini_codec, ce::v3::json_document>("null"sv),
                                  document_from<counting_codec, ce::v3::json_document>("null"sv),
                                  document_from<other_codec, ce::v3::json_document>("null"sv)}) {
      expect(json_null.dump() == gone.dump()) << "both serialise as null";
      expect(!(gone == json_null)) << "moved-from on the left";
      expect(!(json_null == gone)) << "moved-from on the right";
      expect(gone != json_null);
      expect(json_null != gone);
    }
  };

  "an event with a moved-from payload differs from one carrying null in either order"_test = [] {
    const auto moved = ce::v4::to_v3(v4_event_with(moved_from_v4()));
    const ce::v3::event with_null{
        "7"_id,
        "/s"_source,
        "t"_type,
        {.data = document_from<mini_codec, ce::v3::json_document>("null"sv)}};
    expect(!(moved == with_null));
    expect(!(with_null == moved));
  };

  "a v3 document copied from a converted one stays moved-from"_test = [] {
    const auto converted = ce::v4::to_v3(v4_event_with(moved_from_v4()));
    const auto copy = std::get<ce::v3::json_document>(converted.data());
    expect(copy == std::get<ce::v3::json_document>(converted.data()));
    expect(copy.dump() == "null"sv);
    expect(copy.get<mini_codec>() == nullptr);
  };
};

}  // namespace

int main() {}
