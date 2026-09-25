#include <boost/ut.hpp>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/result.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "codecs_under_test.hpp"
#include "mini_codec.hpp"
#include "no_nlohmann_probe.hpp"

// The codec seam is an abstraction over a JSON DOM, so every check here is
// written once as a function template and instantiated for both in-tree codecs.
// A check that only ran against nlohmann would be describing nlohmann.

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::codec::nlohmann_codec;
using mini_codec = ce::test::mini_codec;

// --- negative codecs -------------------------------------------------------
//
// Without these, every static_assert below would also pass against a concept
// that accepted anything.

/// A value type and nothing else.
struct value_only_codec {
  using value = int;
};

/// find() returning by value cannot tell an absent member from a present null
/// one, which is the distinction the format layer decides `data` on.
struct find_by_value_codec : mini_codec {
  [[nodiscard]] static auto find(const value& object, std::string_view key) -> value;
};

/// An as_ accessor returning the bare C++ type has nowhere to report a type
/// mismatch in JSON that came from a peer.
struct unchecked_as_int_codec : mini_codec {
  [[nodiscard]] static auto as_int(const value& subject) -> std::int64_t;
};

/// A constructor that does not return the codec's own value type gives the
/// encoder nothing it can store.
struct no_make_object_codec : mini_codec {
  [[nodiscard]] static auto make_object() -> int;
};

template <bool with_identity>
struct identity_base {};
template <>
struct identity_base<true> {
  static constexpr std::string_view identity = "io.cloudevents.cpp.test.partial";
};

/// mini_codec with `equal`, `copy`, `identity`, `extract` or
/// `for_each_mutable_element` switched off. Deriving cannot remove a member, so
/// this forwards every operation instead and constrains the five the v3 concept
/// adds. Its `for_each_element` accepts any visitor, as the in-tree codecs' do.
template <bool with_equal, bool with_copy, bool with_identity = true, bool with_extract = true,
          bool with_mutable_elements = true>
struct partial_codec : identity_base<with_identity> {
  using value = mini_codec::value;

  [[nodiscard]] static auto parse(std::string_view text) -> ce::result<value> {
    return mini_codec::parse(text);
  }
  [[nodiscard]] static auto dump(const value& held) -> std::string {
    return mini_codec::dump(held);
  }
  [[nodiscard]] static auto make_null() -> value { return mini_codec::make_null(); }
  [[nodiscard]] static auto make_bool(bool held) -> value { return mini_codec::make_bool(held); }
  [[nodiscard]] static auto make_int(std::int64_t held) -> value {
    return mini_codec::make_int(held);
  }
  [[nodiscard]] static auto make_double(double held) -> value {
    return mini_codec::make_double(held);
  }
  [[nodiscard]] static auto make_string(std::string_view held) -> value {
    return mini_codec::make_string(held);
  }
  [[nodiscard]] static auto make_array() -> value { return mini_codec::make_array(); }
  [[nodiscard]] static auto make_object() -> value { return mini_codec::make_object(); }
  static void set(value& object, std::string_view key, value member) {
    mini_codec::set(object, key, std::move(member));
  }
  static void push(value& array, value element) { mini_codec::push(array, std::move(element)); }
  [[nodiscard]] static auto find(const value& object, std::string_view key) -> const value* {
    return mini_codec::find(object, key);
  }
  [[nodiscard]] static auto size_of(const value& held) -> std::size_t {
    return mini_codec::size_of(held);
  }
  [[nodiscard]] static auto kind_of(const value& held) -> ce::json::kind {
    return mini_codec::kind_of(held);
  }
  [[nodiscard]] static auto as_bool(const value& held) -> ce::result<bool> {
    return mini_codec::as_bool(held);
  }
  [[nodiscard]] static auto as_int(const value& held) -> ce::result<std::int64_t> {
    return mini_codec::as_int(held);
  }
  [[nodiscard]] static auto as_double(const value& held) -> ce::result<double> {
    return mini_codec::as_double(held);
  }
  [[nodiscard]] static auto as_string(const value& held) -> ce::result<std::string_view> {
    return mini_codec::as_string(held);
  }
  template <class F>
  static void for_each_member(const value& object, F visit) {
    mini_codec::for_each_member(object, std::move(visit));
  }
  template <class F>
  static void for_each_element(const value& array, F visit) {
    mini_codec::for_each_element(array, std::move(visit));
  }

  [[nodiscard]] static auto equal(const value& left, const value& right) -> bool
    requires with_equal
  {
    return mini_codec::equal(left, right);
  }
  [[nodiscard]] static auto copy(const value& held) -> value
    requires with_copy
  {
    return mini_codec::copy(held);
  }
  [[nodiscard]] static auto extract(value& object, std::string_view key) -> value
    requires with_extract
  {
    return mini_codec::extract(object, key);
  }
  template <class F>
  static void for_each_mutable_element(value& array, F visit)
    requires with_mutable_elements
  {
    mini_codec::for_each_mutable_element(array, std::move(visit));
  }
};

/// An identity only known at run time cannot select a fast path in a constant
/// expression, and an empty one names nothing.
struct runtime_identity_codec : mini_codec {
  static inline const std::string identity{"io.cloudevents.cpp.test.runtime"};
};
struct empty_identity_codec : mini_codec {
  static constexpr std::string_view identity{};
};

// --- nlohmann-shape detectors ----------------------------------------------
//
// Templates on purpose: a requires-expression naming a member of a CONCRETE type
// hard-errors instead of evaluating to false.
template <class V>
concept has_subscript = requires(V value) { value["key"]; };
template <class V>
concept has_begin = requires(V value) { value.begin(); };
template <class V>
concept has_dump_member = requires(V value) { value.dump(); };

// --- shared checks ---------------------------------------------------------

constexpr auto sample_document =
    R"({"a":1,"b":"x","c":[1,2],"d":null,"e":1.5,"f":true})"sv;

/// Longer than the buffer any codec starts a dump in, so the dump has to grow.
constexpr std::size_t text_longer_than_a_first_dump_buffer = 1024;

/// A codec writes its dump straight into the string it returns and looks a
/// member up by the caller's view of the key. So the dump must survive growing
/// and escaping, and a key view that is not null-terminated must match exactly
/// its own characters.
template <class C>
void check_dump_grows_and_keys_are_views(std::string_view label) {
  using namespace boost::ut;

  const std::string long_text =
      std::string(text_longer_than_a_first_dump_buffer, 'x') + "\"\\\n\xC3\xA9";
  auto object = C::make_object();
  C::set(object, "long", C::make_string(long_text));

  auto again = C::parse(C::dump(object));
  expect(again.has_value()) << label << ": re-parse of a grown dump";
  if (!again) {
    return;
  }

  constexpr std::string_view longer_key = "longer";
  const std::string_view key = longer_key.substr(0, std::string_view{"long"}.size());
  const auto* found = C::find(*again, key);
  expect(found != nullptr) << label << ": find by a view that is not null-terminated";
  if (found != nullptr) {
    const auto text = C::as_string(*found);
    expect(text.has_value() && *text == long_text) << label;
  }
  expect(C::find(*again, longer_key) == nullptr) << label;
  expect(C::find(*again, key.substr(1)) == nullptr) << label;

  auto extracted = C::extract(*again, key);
  const auto extracted_text = C::as_string(extracted);
  expect(extracted_text.has_value() && *extracted_text == long_text) << label << ": extract";
}

template <class C>
void check_parse_dump_roundtrip(std::string_view label) {
  using namespace boost::ut;

  auto document = C::parse(sample_document);
  expect(document.has_value()) << label << ": parse";
  if (!document) {
    return;
  }

  // dump() must produce text that parses back to the same document. The exact
  // spelling is the codec's business; the identity is not.
  const auto text = C::dump(*document);
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(text)>, std::string>);
  auto again = C::parse(text);
  expect(again.has_value()) << label << ": re-parse of dump";
  if (!again) {
    return;
  }
  expect(C::size_of(*again) == C::size_of(*document)) << label << ": member count";
  const auto* integer = C::find(*again, "a");
  expect(integer != nullptr) << label;
  if (integer != nullptr) {
    const auto held = C::as_int(*integer);
    expect(held.has_value() && *held == 1) << label;
  }

  for (const auto bad : {"{"sv, R"({"a":})"sv, "[1,"sv, "tru"sv, ""sv}) {
    auto rejected = C::parse(bad);
    expect(!rejected.has_value()) << label << ": should reject " << bad;
    if (!rejected) {
      expect(rejected.error().code == ce::errc::parse_error) << label << " on " << bad;
    }
  }

  // A default-constructed view has a null data(), which ""sv does not: that one
  // points at a string literal. A codec handing the null straight to a parser
  // that only asserts on it reports nothing in a release build.
  auto empty = C::parse(std::string_view{});
  expect(!empty.has_value()) << label << ": should reject a default-constructed view";
  if (!empty) {
    expect(empty.error().code == ce::errc::parse_error) << label;
  }
}

template <class C>
void check_value_constructors(std::string_view label) {
  using namespace boost::ut;

  expect(C::kind_of(C::make_null()) == ce::json::kind::null) << label;
  expect(C::kind_of(C::make_bool(true)) == ce::json::kind::boolean) << label;
  expect(C::kind_of(C::make_int(7)) == ce::json::kind::integer) << label;
  expect(C::kind_of(C::make_double(1.5)) == ce::json::kind::floating) << label;
  expect(C::kind_of(C::make_string("text")) == ce::json::kind::string) << label;
  expect(C::kind_of(C::make_array()) == ce::json::kind::array) << label;
  expect(C::kind_of(C::make_object()) == ce::json::kind::object) << label;

  // An array and an object are built empty, so the encoder fills them itself
  // rather than removing anything a constructor put there.
  expect(C::size_of(C::make_array()) == 0U) << label;
  expect(C::size_of(C::make_object()) == 0U) << label;

  const auto boolean = C::as_bool(C::make_bool(false));
  expect(boolean.has_value() && *boolean == false) << label;
  const auto integer = C::as_int(C::make_int(-42));
  expect(integer.has_value() && *integer == -42) << label;
  const auto number = C::as_double(C::make_double(1.5));
  expect(number.has_value()) << label;
  if (number) {
    expect(*number == 1.5_d) << label;
  }
  // The string value is kept alive: as_string views storage the value owns, so
  // calling it on a temporary would dangle.
  const auto string_value = C::make_string("text");
  const auto text = C::as_string(string_value);
  expect(text.has_value()) << label;
  if (text) {
    expect(*text == "text"sv) << label;
  }

  // Same null data() as in parse: an empty attribute value reaches make_string
  // whenever a peer sends one.
  const auto from_null = C::make_string(std::string_view{});
  expect(C::kind_of(from_null) == ce::json::kind::string) << label;
  const auto empty_text = C::as_string(from_null);
  expect(empty_text.has_value()) << label;
  if (empty_text) {
    expect(empty_text->empty()) << label;
  }
}

template <class C>
void check_set_and_push(std::string_view label) {
  using namespace boost::ut;

  auto object = C::make_object();
  C::set(object, "n", C::make_int(7));
  C::set(object, "t", C::make_string("hi"));
  expect(C::size_of(object) == 2U) << label;

  // set() on an existing name replaces rather than appending a second member,
  // which is what makes an encoder writing an attribute twice harmless.
  C::set(object, "n", C::make_int(9));
  expect(C::size_of(object) == 2U) << label << ": set must replace";
  const auto* replaced = C::find(object, "n");
  expect(replaced != nullptr) << label;
  if (replaced != nullptr) {
    const auto held = C::as_int(*replaced);
    expect(held.has_value() && *held == 9) << label;
  }

  // An empty key is not a name the SDK emits, but find() is reached with an
  // arbitrary name whenever a decoder looks one up, and a null data() must not
  // travel into the DOM.
  expect(C::find(object, std::string_view{}) == nullptr) << label;
  C::set(object, std::string_view{}, C::make_int(3));
  const auto* under_empty_key = C::find(object, std::string_view{});
  expect(under_empty_key != nullptr) << label;
  if (under_empty_key != nullptr) {
    const auto held = C::as_int(*under_empty_key);
    expect(held.has_value() && *held == 3) << label;
  }

  auto array = C::make_array();
  C::push(array, C::make_bool(false));
  C::push(array, C::make_int(1));
  expect(C::size_of(array) == 2U) << label;

  // A nested value survives being stored, so the DOM really is nested.
  C::set(object, "l", std::move(array));
  const auto* nested = C::find(object, "l");
  expect(nested != nullptr) << label;
  if (nested != nullptr) {
    expect(C::kind_of(*nested) == ce::json::kind::array) << label;
    expect(C::size_of(*nested) == 2U) << label;
  }
}

template <class C>
void check_find_and_traversal(std::string_view label) {
  using namespace boost::ut;

  auto document = C::parse(sample_document);
  expect(document.has_value()) << label;
  if (!document) {
    return;
  }

  // Absent and present-and-null must be distinguishable: `data` absent is not
  // `data: null`, and the data/data_base64 exclusion is decided on presence.
  expect(C::find(*document, "zz") == nullptr) << label << ": absent must be null pointer";
  const auto* null_member = C::find(*document, "d");
  expect(null_member != nullptr) << label << ": present-and-null must be found";
  if (null_member != nullptr) {
    expect(C::kind_of(*null_member) == ce::json::kind::null) << label;
  }

  std::vector<std::string> names;
  C::for_each_member(*document, [&names](std::string_view name, const auto&) {
    names.emplace_back(name);
  });
  expect(names.size() == 6U) << label << ": for_each_member visit count";

  const auto* array = C::find(*document, "c");
  expect(array != nullptr) << label;
  if (array != nullptr) {
    std::vector<std::int64_t> elements;
    C::for_each_element(*array, [&elements](const auto& element) {
      if (const auto held = C::as_int(element)) {
        elements.push_back(*held);
      }
    });
    expect(elements == std::vector<std::int64_t>{1, 2}) << label << ": for_each_element";
  }

  // Traversal of an empty array and an empty object visits nothing rather than
  // failing, which the empty batch relies on.
  std::size_t visited = 0;
  C::for_each_element(C::make_array(), [&visited](const auto&) { ++visited; });
  C::for_each_member(C::make_object(), [&visited](std::string_view, const auto&) { ++visited; });
  expect(visited == 0U) << label;
}

template <class C>
void check_kind_and_as_accessors(std::string_view label) {
  using namespace boost::ut;

  auto document = C::parse(sample_document);
  expect(document.has_value()) << label;
  if (!document) {
    return;
  }

  expect(C::kind_of(*document) == ce::json::kind::object) << label;

  const auto* integer = C::find(*document, "a");
  const auto* text = C::find(*document, "b");
  const auto* array = C::find(*document, "c");
  const auto* null_member = C::find(*document, "d");
  const auto* number = C::find(*document, "e");
  const auto* boolean = C::find(*document, "f");
  expect((integer != nullptr) && (text != nullptr) && (array != nullptr) &&
         (null_member != nullptr) && (number != nullptr) && (boolean != nullptr))
      << label;
  if (integer == nullptr || text == nullptr || array == nullptr || null_member == nullptr ||
      number == nullptr || boolean == nullptr) {
    return;
  }

  expect(C::kind_of(*integer) == ce::json::kind::integer) << label;
  expect(C::kind_of(*text) == ce::json::kind::string) << label;
  expect(C::kind_of(*array) == ce::json::kind::array) << label;
  expect(C::kind_of(*null_member) == ce::json::kind::null) << label;
  expect(C::kind_of(*boolean) == ce::json::kind::boolean) << label;

  // An integral number and a fractional one are different kinds, because the
  // CloudEvents Integer type is 32-bit signed and 1.0 must not satisfy it.
  expect(C::kind_of(*number) == ce::json::kind::floating) << label;
  expect(!C::as_int(*number).has_value()) << label << ": as_int must reject a float";

  // A mismatch is a returned error, not undefined behaviour.
  const auto wrong_bool = C::as_bool(*text);
  expect(!wrong_bool.has_value()) << label;
  if (!wrong_bool) {
    expect(wrong_bool.error().code == ce::errc::type_mismatch) << label;
  }
  const auto wrong_string = C::as_string(*integer);
  expect(!wrong_string.has_value()) << label;
  if (!wrong_string) {
    expect(wrong_string.error().code == ce::errc::type_mismatch) << label;
  }
  expect(!C::as_int(*text).has_value()) << label;
  expect(!C::as_double(*text).has_value()) << label;
  expect(!C::as_bool(*null_member).has_value()) << label;

  const auto good_int = C::as_int(*integer);
  expect(good_int.has_value() && *good_int == 1) << label;
  const auto good_string = C::as_string(*text);
  expect(good_string.has_value() && *good_string == "x"sv) << label;
  const auto good_bool = C::as_bool(*boolean);
  expect(good_bool.has_value() && *good_bool == true) << label;

  expect(C::size_of(*document) == 6U) << label;
  expect(C::size_of(*array) == 2U) << label;
}

// spec: SWR-JSON-0001
const boost::ut::suite<"json-codec-concept-dom-surface"> codec_concept_dom_surface = [] {
  using namespace boost::ut;

  "both in-tree codecs satisfy the concept"_test = [] {
    static_assert(ce::json::json_codec<nlohmann_codec>);
    static_assert(ce::json::json_codec<mini_codec>);
    expect(true);
  };

  // The concept is over a JSON DOM and nothing else: mini_codec.hpp includes
  // neither core.hpp nor anything that knows what an event is, yet satisfies it.
  "the surface is a nested value type plus operations over it"_test = [] {
    static_assert(ce::json::json_codec<mini_codec>);
    static_assert(std::movable<mini_codec::value>);
    static_assert(std::movable<nlohmann_codec::value>);
    // A value holds values: the DOM nests.
    auto object = mini_codec::make_object();
    mini_codec::set(object, "inner", mini_codec::make_object());
    const auto* inner = mini_codec::find(object, "inner");
    expect(inner != nullptr);
    if (inner != nullptr) {
      expect(mini_codec::kind_of(*inner) == ce::json::kind::object);
    }
  };

  // Without these the static_asserts above would also hold for a concept that
  // constrained nothing.
  "a type that is not a JSON DOM does not satisfy it"_test = [] {
    static_assert(!ce::json::json_codec<value_only_codec>);
    static_assert(!ce::json::json_codec<int>);
    static_assert(!ce::json::json_codec<nlohmann_codec::value>);
    static_assert(!ce::json::json_codec<no_make_object_codec>);
    expect(true);
  };
};

// spec: SWR-JSON-0002
const boost::ut::suite<"json-codec-parse-dump-roundtrip"> codec_parse_dump_roundtrip = [] {
  using namespace boost::ut;

  "parse returns a result and dump returns a std::string"_test = [] {
    static_assert(std::is_same_v<decltype(nlohmann_codec::parse(""sv)),
                                 ce::result<nlohmann_codec::value>>);
    static_assert(
        std::is_same_v<decltype(mini_codec::parse(""sv)), ce::result<mini_codec::value>>);
    static_assert(
        std::is_same_v<decltype(nlohmann_codec::dump(std::declval<const nlohmann_codec::value&>())),
                       std::string>);
    static_assert(
        std::is_same_v<decltype(mini_codec::dump(std::declval<const mini_codec::value&>())),
                       std::string>);
    expect(true);
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_parse_dump_roundtrip<C>(codec); };
    test(std::string{codec} + " grown dump, key views") = [codec] {
      check_dump_grows_and_keys_are_views<C>(codec);
    };
  });
};

// spec: SWR-JSON-0003
const boost::ut::suite<"json-codec-value-constructors"> codec_value_constructors = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_value_constructors<C>(codec); };
  });
};

// spec: SWR-JSON-0004
const boost::ut::suite<"json-codec-set-and-push"> codec_set_and_push = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_set_and_push<C>(codec); };
  });
};

// spec: SWR-JSON-0005
const boost::ut::suite<"json-codec-find-and-traversal"> codec_find_and_traversal = [] {
  using namespace boost::ut;

  "find returns a pointer, so absent is distinguishable from null"_test = [] {
    static_assert(
        std::is_same_v<decltype(nlohmann_codec::find(
                           std::declval<const nlohmann_codec::value&>(), ""sv)),
                       const nlohmann_codec::value*>);
    static_assert(!ce::json::json_codec<find_by_value_codec>);
    expect(true);
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_find_and_traversal<C>(codec); };
  });
};

// spec: SWR-JSON-0006
const boost::ut::suite<"json-codec-kind-and-as-accessors"> codec_kind_and_as_accessors = [] {
  using namespace boost::ut;

  "an as_ accessor must return a result"_test = [] {
    static_assert(std::is_same_v<decltype(mini_codec::as_int(
                                     std::declval<const mini_codec::value&>())),
                                 ce::result<std::int64_t>>);
    static_assert(!ce::json::json_codec<unchecked_as_int_codec>);
    expect(true);
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_kind_and_as_accessors<C>(codec); };
  });
};

// spec: SWR-JSON-0007
const boost::ut::suite<"nlohmann-codec-non-throwing-parse"> nlohmann_non_throwing_parse = [] {
  using namespace boost::ut;

  "malformed text is errc::parse_error, not an exception"_test = [] {
    for (const auto bad : {"{"sv, R"({"a":})"sv, "[1,"sv, "tru"sv, ""sv, R"({"a":1,})"sv,
                           R"("unterminated)"sv, "\xff\xfe"sv}) {
#if defined(__cpp_exceptions) && __cpp_exceptions
      bool threw = false;
      bool rejected = false;
      try {
        auto parsed = nlohmann_codec::parse(bad);
        rejected = !parsed.has_value() && parsed.error().code == ce::errc::parse_error;
      } catch (...) {
        threw = true;
      }
      expect(!threw) << "parse threw on " << bad;
      expect(rejected) << "parse did not report parse_error on " << bad;
#else
      auto parsed = nlohmann_codec::parse(bad);
      expect(!parsed.has_value()) << "parse accepted " << bad;
      if (!parsed) {
        expect(parsed.error().code == ce::errc::parse_error) << bad;
      }
#endif
    }
  };

  // Deeply nested input is the other way nlohmann's throwing parse shows up.
  "a pathologically nested document is a returned error or a value, never a throw"_test = [] {
    std::string nested(512, '[');
    nested.append(512, ']');
    auto parsed = nlohmann_codec::parse(nested);
    expect(parsed.has_value() || parsed.error().code == ce::errc::parse_error);
  };

  "well-formed text still parses"_test = [] {
    auto parsed = nlohmann_codec::parse(sample_document);
    expect(parsed.has_value());
  };
};

// spec: SWR-JSON-0008
const boost::ut::suite<"no-nlohmann-type-outside-codec-header"> no_nlohmann_outside_codec = [] {
  using namespace boost::ut;

  // A test cannot grep the headers, so the assertion is made by compilation
  // instead: no_nlohmann_probe.cpp includes every public SDK header EXCEPT
  // include/cloudevents/codec/nlohmann.hpp, and stops the build with #error if
  // nlohmann's version macro is defined there. It also drives json_format over a
  // user-supplied codec in that same nlohmann-free translation unit, which is
  // the behaviour the requirement exists to protect.
  //
  // NOT covered here: a header that names nlohmann in a template that is never
  // instantiated AND forward-declares it rather than including it would leave
  // the macro undefined. Nothing in the tree does that today, and the
  // no-default-codec preset, where nlohmann is not on the include path at all,
  // catches it if anything starts to.
  "the SDK headers compile with no nlohmann in the translation unit"_test = [] {
    const auto observed = ce_no_nlohmann::probe();
    expect(!observed.nlohmann_macro_defined)
        << "nlohmann leaked into a translation unit that includes no codec header";
    expect(observed.format_round_tripped) << "json_format did not work without nlohmann";
    expect(!observed.encoded.empty());
  };

  // The detector is not vacuous: THIS translation unit does include the codec
  // header, and here the same macro is defined.
  "the detector fires where nlohmann really is included"_test = [] {
#ifdef NLOHMANN_JSON_VERSION_MAJOR
    expect(true);
#else
    expect(false) << "NLOHMANN_JSON_VERSION_MAJOR is undefined even with the codec header included";
#endif
  };
};

// spec: SWR-JSON-0009
const boost::ut::suite<"mini-codec-satisfies-json-codec-concept"> mini_codec_satisfies = [] {
  using namespace boost::ut;

  "mini_codec satisfies the concept independently of nlohmann"_test = [] {
    static_assert(ce::json::json_codec<mini_codec>);
    static_assert(!std::is_same_v<mini_codec::value, nlohmann_codec::value>);
    expect(true);
  };

  // A concept exercised by one implementation is a concept shaped around that
  // implementation, so mini_codec is deliberately not nlohmann-shaped.
  "mini_codec offers none of nlohmann's incidental surface"_test = [] {
    static_assert(!has_subscript<mini_codec::value>);
    static_assert(!has_begin<mini_codec::value>);
    static_assert(!has_dump_member<mini_codec::value>);
    // ...and nlohmann's value does have all three, so the detectors work.
    static_assert(has_subscript<nlohmann_codec::value>);
    static_assert(has_begin<nlohmann_codec::value>);
    static_assert(has_dump_member<nlohmann_codec::value>);
    expect(true);
  };

  // The json_format suites instantiate over mini_codec too; this is the
  // codec-level half of that pairing.
  "mini_codec parses and dumps a real document"_test = [] {
    check_parse_dump_roundtrip<mini_codec>("mini_codec");
  };
};

template <class C>
void check_equal_and_copy(std::string_view label) {
  using namespace boost::ut;

  const auto document = C::parse(R"({"a":1,"b":[1,2],"c":{"x":"y","z":null}})"sv);
  const auto reordered =
      C::parse(R"( { "c" : { "z" : null, "x" : "y" }, "b" : [1, 2], "a" : 1 } )"sv);
  const auto other_value = C::parse(R"({"a":2,"b":[1,2],"c":{"x":"y","z":null}})"sv);
  const auto other_order = C::parse(R"({"a":1,"b":[2,1],"c":{"x":"y","z":null}})"sv);
  const auto extra_member = C::parse(R"({"a":1,"b":[1,2],"c":{"x":"y","z":null},"d":0})"sv);
  expect(document.has_value() && reordered.has_value() && other_value.has_value() &&
         other_order.has_value() && extra_member.has_value())
      << label << ": parse";
  if (!document || !reordered || !other_value || !other_order || !extra_member) {
    return;
  }

  expect(C::equal(*document, *document)) << label << ": a value equals itself";
  expect(C::equal(*document, *reordered)) << label << ": member order and whitespace do not count";
  expect(C::equal(*reordered, *document)) << label << ": equality is symmetric";
  expect(!C::equal(*document, *other_value)) << label << ": a different member value counts";
  expect(!C::equal(*document, *other_order)) << label << ": element order counts";
  expect(!C::equal(*document, *extra_member)) << label << ": an extra member counts";
  expect(!C::equal(*extra_member, *document)) << label << ": a missing member counts";
  expect(!C::equal(C::make_string("1"), C::make_int(1))) << label << ": kinds differ";

  auto duplicate = C::copy(*document);
  expect(C::equal(duplicate, *document)) << label << ": a copy equals its source";
  C::set(duplicate, "a", C::make_int(3));
  expect(!C::equal(duplicate, *document)) << label << ": the copy is deep";
  const auto* source_member = C::find(*document, "a");
  expect(source_member != nullptr) << label;
  if (source_member != nullptr) {
    const auto held = C::as_int(*source_member);
    expect(held.has_value() && *held == 1) << label << ": changing a copy leaves the source";
  }
}

template <class C>
void check_extract(std::string_view label) {
  using namespace boost::ut;

  auto document = C::parse(R"({"keep":"k","data":{"x":[1,2],"y":"z"}})"sv);
  const auto expected = C::parse(R"({"x":[1,2],"y":"z"})"sv);
  expect(document.has_value() && expected.has_value()) << label << ": parse";
  if (!document || !expected) {
    return;
  }

  const auto moved = C::extract(*document, "data");
  expect(C::equal(moved, *expected)) << label << ": the member arrives whole";
  const auto* kept = C::find(*document, "keep");
  const auto kept_text = kept == nullptr ? ce::result<std::string_view>{} : C::as_string(*kept);
  expect(kept_text.has_value() && *kept_text == "k"sv) << label << ": the other members stay";
  expect(C::dump(*document).size() > 0U) << label << ": the object stays usable";

  const auto before = C::copy(*document);
  const auto absent = C::extract(*document, "missing");
  expect(C::kind_of(absent) == ce::json::kind::null) << label << ": an absent key gives null";
  expect(C::equal(*document, before)) << label << ": an absent key leaves the object";

  auto scalar = C::make_int(7);
  const auto from_scalar = C::extract(scalar, "data");
  expect(C::kind_of(from_scalar) == ce::json::kind::null) << label << ": a non-object gives null";
  const auto scalar_held = C::as_int(scalar);
  expect(scalar_held.has_value() && *scalar_held == 7) << label << ": a non-object is left alone";
}

template <class C>
void check_mutable_elements(std::string_view label) {
  using namespace boost::ut;

  auto batch = C::parse(R"([{"data":1,"id":"a"},{"data":2,"id":"b"}])"sv);
  const auto emptied = C::parse(R"([{"data":null,"id":"a"},{"data":null,"id":"b"}])"sv);
  expect(batch.has_value() && emptied.has_value()) << label << ": parse";
  if (!batch || !emptied) {
    return;
  }

  std::vector<std::int64_t> moved;
  C::for_each_mutable_element(*batch, [&moved](typename C::value& element) {
    const auto held = C::as_int(C::extract(element, "data"));
    moved.push_back(held ? *held : -1);
  });
  expect(moved == std::vector<std::int64_t>{1, 2}) << label << ": every element, in order";
  expect(C::equal(*batch, *emptied)) << label << ": the moves land in the array itself";

  std::size_t visited = 0;
  auto empty = C::make_array();
  C::for_each_mutable_element(empty, [&visited](typename C::value&) { ++visited; });
  expect(visited == 0U) << label << ": an empty array has no elements";
}

// spec: SWR-JSON-0039
const boost::ut::suite<"json-codec-requires-equal-copy-and-identity"> v3_codec_surface = [] {
  using namespace boost::ut;

  "every codec under test satisfies the v3 concept"_test = [] {
    static_assert(ce::v3::json::json_codec<nlohmann_codec>);
    static_assert(ce::v3::json::json_codec<mini_codec>);
    ce_test::for_each_codec([]<class C>(std::string_view) {
      static_assert(ce::v3::json::json_codec<C>);
    });
    expect(true);
  };

  "a codec without equal or copy satisfies only the v1 concept"_test = [] {
    static_assert(ce::v3::json::json_codec<partial_codec<true, true>>);
    static_assert(ce::v1::json::json_codec<partial_codec<false, true>>);
    static_assert(!ce::v3::json::json_codec<partial_codec<false, true>>);
    static_assert(ce::v1::json::json_codec<partial_codec<true, false>>);
    static_assert(!ce::v3::json::json_codec<partial_codec<true, false>>);
    static_assert(!ce::v3::json::json_codec<partial_codec<false, false>>);
    expect(true);
  };

  "a codec without extract satisfies only the v1 concept"_test = [] {
    static_assert(ce::v1::json::json_codec<partial_codec<true, true, true, false>>);
    static_assert(!ce::v3::json::json_codec<partial_codec<true, true, true, false>>);
    expect(true);
  };

  "a codec without the mutable element walk satisfies only the v1 concept"_test = [] {
    // Its const for_each_element accepts any visitor, so a value& argument
    // would bind to it; the concept asks for the walk by its own name.
    static_assert(ce::v1::json::json_codec<partial_codec<true, true, true, true, false>>);
    static_assert(!ce::v3::json::json_codec<partial_codec<true, true, true, true, false>>);
    expect(true);
  };

  "a codec without a constant, non-empty identity satisfies only the v1 concept"_test = [] {
    static_assert(ce::v1::json::json_codec<partial_codec<true, true, false>>);
    static_assert(!ce::v3::json::json_codec<partial_codec<true, true, false>>);
    static_assert(ce::v1::json::json_codec<runtime_identity_codec>);
    static_assert(!ce::v3::json::json_codec<runtime_identity_codec>);
    static_assert(ce::v1::json::json_codec<empty_identity_codec>);
    static_assert(!ce::v3::json::json_codec<empty_identity_codec>);
    expect(true);
  };

  "the codecs under test declare pairwise distinct identities"_test = [] {
    static_assert(nlohmann_codec::identity != mini_codec::identity);
    std::vector<std::string_view> identities;
    ce_test::for_each_codec([&identities]<class C>(std::string_view) {
      identities.push_back(C::identity);
    });
    for (std::size_t first = 0; first < identities.size(); ++first) {
      for (std::size_t second = first + 1; second < identities.size(); ++second) {
        expect(identities.at(first) != identities.at(second))
            << identities.at(first) << " is declared twice";
      }
    }
    expect(identities.size() >= 2U);
  };

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_equal_and_copy<C>(codec); };
    test(std::string{codec} + " extract") = [codec] { check_extract<C>(codec); };
    test(std::string{codec} + " mutable elements") = [codec] {
      check_mutable_elements<C>(codec);
    };
  });
};

// spec: SWR-JSON-0030
const boost::ut::suite<"build-without-default-codec-has-no-nlohmann"> without_default_codec = [] {
  using namespace boost::ut;

  // The real enforcement of this requirement is the no-default-codec CI preset:
  // with CE_DEFAULT_CODEC=OFF the ce_codec_nlohmann target does not exist, this
  // executable is not registered at all, and nlohmann is never fetched or found.
  // A process that has already linked nlohmann cannot observe its own absence,
  // so what is asserted in-process is the property that makes the OFF build
  // possible: the format layer is complete over a codec the SDK does not ship,
  // compiled in a translation unit with no nlohmann in it.
  "json_format is complete over a codec the SDK does not ship"_test = [] {
    const auto observed = ce_no_nlohmann::probe();
    expect(!observed.nlohmann_macro_defined);
    expect(observed.format_round_tripped);
  };

  "no SDK header outside the codec header names nlohmann"_test = [] {
    static_assert(ce::json::json_codec<mini_codec>);
    // json_codec.hpp, json_format.hpp and base64.hpp are the JSON layer, and
    // none of them mentions a codec by name: the format is a template parameter.
    static_assert(!std::is_same_v<mini_codec::value, nlohmann_codec::value>);
    expect(true);
  };
};

// A malformed or out-of-range number must come back as a parse_error rather
// than an exception: the SDK builds with exceptions disabled, and mini_codec is
// compiled into those builds.
const boost::ut::suite<"mini-codec-number-parsing-never-throws"> mini_codec_numbers = [] {
  using namespace boost::ut;

  "malformed and out-of-range numbers are parse errors"_test = [] {
    for (const auto document : {
             R"({"a":+m})"sv,
             R"({"a":+})"sv,
             R"({"a":-})"sv,
             R"({"a":1.2.3})"sv,
             R"({"a":1e})"sv,
             R"({"a":99999999999999999999999})"sv,
             R"({"a":1e999999})"sv,
         }) {
      auto parsed = mini_codec::parse(document);
      expect(!parsed.has_value()) << "should reject " << document;
      if (!parsed) {
        expect(parsed.error().code == ce::errc::parse_error) << document;
      }
    }
  };

  "ordinary numbers still parse"_test = [] {
    auto parsed = mini_codec::parse(R"({"i":-42,"d":1.5})");
    expect(parsed.has_value());
    if (parsed) {
      const auto* i = mini_codec::find(*parsed, "i");
      const auto* d = mini_codec::find(*parsed, "d");
      expect(i != nullptr);
      expect(d != nullptr);
      if (i != nullptr && d != nullptr) {
        auto as_int = mini_codec::as_int(*i);
        auto as_double = mini_codec::as_double(*d);
        expect(as_int.has_value());
        expect(as_double.has_value());
        if (as_int && as_double) {
          expect(*as_int == std::int64_t{-42});
          expect(*as_double == 1.5_d);
        }
      }
    }
  };
};

// A character outside the BMP is written as a surrogate PAIR, and the Java
// CloudEvents SDK writes one for every emoji. Both in-tree codecs got this
// wrong until the interop corpus carried a Java document with one.
const boost::ut::suite<"codecs-decode-surrogate-pairs"> surrogate_pairs = [] {
  using namespace boost::ut;

  // Built at run time: a compiler may reinterpret an escape in a source
  // literal, which hid this for one whole debugging round.
  const auto escaped = [](std::string_view hex) {
    std::string out;
    out += static_cast<char>(92);
    out += 'u';
    out += hex;
    return out;
  };

  "a surrogate pair becomes one code point"_test = [&escaped] {
    const std::string document =
        "{\"s\":\"" + escaped("D83D") + escaped("DE00") + "\"}";

    for (auto parsed : {mini_codec::parse(document)}) {
      expect(parsed.has_value());
      if (!parsed) {
        continue;
      }
      const auto* member = mini_codec::find(*parsed, "s");
      expect(member != nullptr);
      if (member == nullptr) {
        continue;
      }
      auto text = mini_codec::as_string(*member);
      expect(text.has_value());
      if (text) {
        // Named as BYTES, not as a character literal. This test is about the
        // byte sequence, and a universal-character-name is the thing a
        // compiler may reinterpret - which is what hid the defect twice. MSVC
        // additionally cannot represent this one in its default code page.
        const std::string expected{"\xF0\x9F\x98\x80"};
        expect(text->size() == 4_ul) << "expected 4 bytes, got " << text->size();
        expect(std::string{*text} == expected);
      }
    }
  };

  "both codecs agree with each other"_test = [&escaped] {
    const std::string document =
        "{\"s\":\"" + escaped("D83D") + escaped("DE00") + escaped("00E9") + "\"}";

    auto a = nlohmann_codec::parse(document);
    auto b = mini_codec::parse(document);
    expect(a.has_value());
    expect(b.has_value());
    if (!a || !b) {
      return;
    }
    auto left = nlohmann_codec::as_string(*nlohmann_codec::find(*a, "s"));
    auto right = mini_codec::as_string(*mini_codec::find(*b, "s"));
    expect(left.has_value());
    expect(right.has_value());
    if (left && right) {
      expect(std::string{*left} == std::string{*right});
    }
  };

  "a lone surrogate is refused rather than encoded"_test = [&escaped] {
    // Encoding one on its own is how CESU-8 gets produced, and the result is
    // not valid UTF-8.
    for (const auto hex : {"D83D"sv, "DE00"sv}) {
      const std::string document = "{\"s\":\"" + escaped(hex) + "\"}";
      auto parsed = mini_codec::parse(document);
      expect(!parsed.has_value()) << "should reject a lone " << hex;
      if (!parsed) {
        expect(parsed.error().code == ce::errc::parse_error);
      }
    }
  };
};

/// \brief An integer a codec cannot represent must never become a value.
///
/// A codec may refuse the document at parse or refuse it at as_int; both are
/// conformant. Returning a number that is not the one on the wire is not.
template <class C>
void check_integer_range(std::string_view label) {
  using namespace boost::ut;

  // 2^63 reinterprets as INT64_MIN; 2^64-1 reinterprets as -1, which is inside
  // the CloudEvents Integer range and so is NOT caught by the format layer's
  // own bounds check. That second one is the case that corrupted silently.
  for (const auto document : {R"({"n":9223372036854775808})"sv,
                              R"({"n":18446744073709551615})"sv}) {
    auto parsed = C::parse(document);
    if (!parsed) {
      expect(parsed.error().code == ce::errc::parse_error) << label << ": " << document;
      continue;
    }
    const auto* member = C::find(*parsed, "n");
    expect(member != nullptr) << label << ": " << document;
    if (member == nullptr) {
      continue;
    }
    // It is an integer, whatever its magnitude.
    expect(C::kind_of(*member) == ce::json::kind::integer) << label << ": " << document;

    auto held = C::as_int(*member);
    expect(!held.has_value()) << label << ": " << document << " must not yield a value";
    if (!held) {
      expect(held.error().code == ce::errc::out_of_range) << label << ": " << document;
    }
  }

  // The largest value that DOES fit is still accepted, so the refusal is about
  // the range rather than about large numbers.
  auto fits = C::parse(R"({"n":9223372036854775807})");
  if (fits) {
    const auto* member = C::find(*fits, "n");
    expect(member != nullptr) << label;
    if (member != nullptr) {
      auto held = C::as_int(*member);
      expect(held.has_value()) << label;
      if (held) {
        expect(*held == std::numeric_limits<std::int64_t>::max()) << label;
      }
    }
  }
}

/// \brief kind_of separates integers from reals by the TEXT, not the magnitude.
template <class C>
void check_number_kinds(std::string_view label) {
  using namespace boost::ut;

  const auto kind_of_member = [&label](std::string_view document) -> std::optional<ce::json::kind> {
    auto parsed = C::parse(document);
    if (!parsed) {
      return std::nullopt;
    }
    const auto* member = C::find(*parsed, "n");
    expect(member != nullptr) << label << ": " << document;
    return member == nullptr ? std::nullopt : std::optional{C::kind_of(*member)};
  };

  for (const auto document : {R"({"n":1})"sv, R"({"n":-1})"sv, R"({"n":0})"sv}) {
    auto kind = kind_of_member(document);
    expect(kind.has_value()) << label << ": " << document;
    if (kind) {
      expect(*kind == ce::json::kind::integer) << label << ": " << document;
    }
  }

  for (const auto document : {R"({"n":1.0})"sv, R"({"n":1.5})"sv, R"({"n":1e3})"sv}) {
    auto kind = kind_of_member(document);
    expect(kind.has_value()) << label << ": " << document;
    if (kind) {
      // 1.0 is a real, not an integer: SWR-JSON-0024 turns on exactly this.
      expect(*kind == ce::json::kind::floating) << label << ": " << document;
    }
  }

  // Past int64 in both directions, it is STILL an integer. A codec that said
  // floating here would make the format layer report a fractional value.
  for (const auto document : {R"({"n":9223372036854775808})"sv,
                              R"({"n":18446744073709551615})"sv}) {
    if (auto kind = kind_of_member(document)) {
      expect(*kind == ce::json::kind::integer) << label << ": " << document;
    }
  }
}

// spec: SWR-JSON-0033
const boost::ut::suite<"integer-out-of-range-never-yields-a-value"> integer_range = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_integer_range<C>(codec); };
  });
};

// spec: SWR-JSON-0034
const boost::ut::suite<"number-kind-follows-the-text-not-the-magnitude"> number_kinds = [] {
  using namespace boost::ut;

  ce_test::for_each_codec([]<class C>(std::string_view codec) {
    test(std::string{codec}) = [codec] { check_number_kinds<C>(codec); };
  });
};

/// \brief Every codec header names its own JSON library and no other.
///
/// A codec that reached for a second library would compile on any developer
/// machine that has both installed, and fail only in a CI job that deliberately
/// installs one. A text scan finds it in a second.
const boost::ut::suite<"codec-headers-are-mutually-isolated"> codec_isolation = [] {
  using namespace boost::ut;

  // Comments are stripped first. The check is about what a header DEPENDS on,
  // and a doc comment explaining how another codec behaves differently is not a
  // dependency - rapidjson.hpp says nlohmann's DOM is node-stable, and saying so
  // is the point of the comment.
  const auto read_code = [](const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    std::string code;
    code.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
      if (source.compare(i, 2, "//") == 0) {
        while (i < source.size() && source[i] != '\n') {
          ++i;
        }
        code.push_back('\n');
      } else if (source.compare(i, 2, "/*") == 0) {
        const auto end = source.find("*/", i + 2);
        i = end == std::string::npos ? source.size() : end + 1;
      } else {
        code.push_back(source[i]);
      }
    }
    return code;
  };

  // library token -> the one codec header allowed to name it
  const std::array<std::pair<std::string_view, std::string_view>, 3> libraries{{
      {"nlohmann", "nlohmann.hpp"},
      {"rapidjson", "rapidjson.hpp"},
      {"boost", "boost_json.hpp"},
  }};

  const std::filesystem::path codec_dir{std::string{CE_INCLUDE_DIR} + "/cloudevents/codec"};

  "the codec directory is where this test thinks it is"_test = [codec_dir] {
    // Otherwise every assertion below passes over an empty range.
    expect(std::filesystem::is_directory(codec_dir)) << codec_dir.string();
    std::size_t headers = 0;
    for (const auto& entry : std::filesystem::directory_iterator{codec_dir}) {
      headers += entry.path().extension() == ".hpp" ? 1 : 0;
    }
    expect(headers > 0_ul) << "no codec headers found";
  };

  "a codec header names only its own library"_test = [&read_code, &libraries, codec_dir] {
    if (!std::filesystem::is_directory(codec_dir)) {
      return;
    }
    for (const auto& entry : std::filesystem::directory_iterator{codec_dir}) {
      if (entry.path().extension() != ".hpp") {
        continue;
      }
      const auto name = entry.path().filename().string();
      const auto source = read_code(entry.path());
      for (const auto& [token, owner] : libraries) {
        if (name == owner) {
          expect(source.find(token) != std::string::npos)
              << name << " no longer names " << token;
          continue;
        }
        expect(source.find(token) == std::string::npos)
            << name << " names " << token << ", which belongs to " << owner;
      }
    }
  };

  "no header outside codec/ names a JSON library"_test = [&read_code, &libraries] {
    const std::filesystem::path root{std::string{CE_INCLUDE_DIR} + "/cloudevents"};
    if (!std::filesystem::is_directory(root)) {
      return;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
      if (!entry.is_regular_file() || entry.path().extension() != ".hpp") {
        continue;
      }
      const auto relative = std::filesystem::relative(entry.path(), root).generic_string();
      if (relative.starts_with("codec/")) {
        continue;
      }
      const auto source = read_code(entry.path());
      for (const auto& [token, owner] : libraries) {
        expect(source.find(token) == std::string::npos)
            << relative << " names " << token << ", which only " << owner << " may";
      }
    }
  };
};

}  // namespace

int main() {}
