#include <boost/ut.hpp>

#include <initializer_list>

#include <cloudevents/v2/core.hpp>
#include <cloudevents/detail/timestamp.hpp>
#include <cloudevents/result.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if CE_HAS_EXPECTED
#include <expected>
#endif

// The core is the one layer with no I/O and no codec, so everything it promises is
// checkable in-process: the attribute type system, the RFC 3339 parser, the
// validator and the error carrier. The constexpr validators are asserted with
// static_assert rather than expect, because a validator that is only usable at run
// time has already broken its contract (SPEC section 6).

namespace {

using namespace std::string_view_literals;

// Detectors for the enforced result<T> subset. These are templates on purpose: a
// requires-expression naming a member of a CONCRETE type hard-errors instead of
// evaluating to false, so a non-template detector could not express "this member
// is absent" at all.
template <class T>
concept has_value_fn = requires(T r) { r.value(); };
template <class T>
concept has_value_or = requires(T r) { r.value_or(0); };
template <class T>
concept has_error_or = requires(T r) { r.error_or(ce::v2::error{.code = ce::v2::errc::parse_error}); };
template <class T>
concept has_and_then = requires(T r) { r.and_then([](auto&&) { return T{}; }); };
template <class T>
concept has_or_else = requires(T r) { r.or_else([](auto&&) { return T{}; }); };
template <class T>
concept has_transform = requires(T r) { r.transform([](auto&& v) { return v; }); };

using namespace ce::v2::literals;

/// A minimal event, built in ONE expression from literals the compiler checked.
/// The caller states every optional attribute it wants in `rest`.
[[nodiscard]] auto good_event(ce::v2::event::options rest = {}) -> ce::v2::event {
  return ce::v2::event{"1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

/// True when `made` failed with `code`, naming `where`.
template <class T>
[[nodiscard]] auto refused(const ce::v2::result<T>& made, ce::v2::errc code, std::string_view where)
    -> bool {
  return !made.has_value() && made.error().code == code && made.error().where == where;
}

/// True when canonical input survives parse plus render byte for byte.
[[nodiscard]] auto round_trips(std::string_view text) -> bool {
  const auto parsed = ce::v2::parse_timestamp(text);
  return parsed.has_value() && ce::v2::to_string(*parsed) == text;
}

/// True when the parser refuses the input outright.
[[nodiscard]] auto rejected(std::string_view text) -> bool {
  return !ce::v2::parse_timestamp(text).has_value();
}

const boost::ut::suite<"core-error-type"> core_error_type = [] {
  using namespace boost::ut;

  // A switch with no default means adding an enumerator and forgetting to name it
  // is a -Werror=switch failure rather than a silent "unknown" at run time.
  "every errc enumerator has a name, in a constant expression"_test = [] {
    static_assert(ce::v2::to_string_view(ce::v2::errc::missing_required_attribute) ==
                  "missing_required_attribute");
    static_assert(ce::v2::to_string_view(ce::v2::errc::invalid_attribute_name) == "invalid_attribute_name");
    static_assert(ce::v2::to_string_view(ce::v2::errc::reserved_attribute_name) ==
                  "reserved_attribute_name");
    static_assert(ce::v2::to_string_view(ce::v2::errc::invalid_attribute_value) ==
                  "invalid_attribute_value");
    static_assert(ce::v2::to_string_view(ce::v2::errc::unsupported_spec_version) ==
                  "unsupported_spec_version");
    static_assert(ce::v2::to_string_view(ce::v2::errc::invalid_timestamp) == "invalid_timestamp");
    static_assert(ce::v2::to_string_view(ce::v2::errc::invalid_content_type) == "invalid_content_type");
    static_assert(ce::v2::to_string_view(ce::v2::errc::parse_error) == "parse_error");
    static_assert(ce::v2::to_string_view(ce::v2::errc::type_mismatch) == "type_mismatch");
    static_assert(ce::v2::to_string_view(ce::v2::errc::out_of_range) == "out_of_range");
    static_assert(ce::v2::to_string_view(ce::v2::errc::data_conflict) == "data_conflict");
    static_assert(ce::v2::to_string_view(ce::v2::errc::invalid_base64) == "invalid_base64");
    static_assert(ce::v2::to_string_view(ce::v2::errc::invalid_utf8) == "invalid_utf8");
    static_assert(ce::v2::to_string_view(ce::v2::errc::not_a_cloudevent) == "not_a_cloudevent");
    expect(true);
  };

  // No `ok` enumerator: an error only exists on the failure path, so a success
  // value would have nothing to name.
  "errc is a closed set with no success value"_test = [] {
    static_assert(std::is_same_v<std::underlying_type_t<ce::v2::errc>, std::uint8_t>);
    expect(ce::v2::to_string_view(ce::v2::errc::parse_error) != ce::v2::to_string_view(ce::v2::errc::type_mismatch));
  };

  "error is an aggregate, built with one designated initializer"_test = [] {
    static_assert(std::is_aggregate_v<ce::v2::error>);

    const ce::v2::error err{
        .code = ce::v2::errc::parse_error,
        .detail = "trailing bytes",
        .where = "/data",
    };
    expect(err.code == ce::v2::errc::parse_error);
    expect(err.detail == "trailing bytes");
    expect(err.where == "/data");
  };

  // The optional members carry a default member initializer, so naming only the
  // required one is well-formed and leaves the rest empty rather than indeterminate.
  "only code is required"_test = [] {
    const ce::v2::error err{.code = ce::v2::errc::out_of_range};
    expect(err.detail.empty());
    expect(err.where.empty());
  };
};

const boost::ut::suite<"core-result-expected-alias"> core_result_expected_alias = [] {
  using namespace boost::ut;

  "result<T> IS std::expected<T, error> wherever the library has it"_test = [] {
#if CE_HAS_EXPECTED
    static_assert(std::is_same_v<ce::v2::result<int>, std::expected<int, ce::v2::error>>);
    static_assert(std::is_same_v<ce::v2::result<void>, std::expected<void, ce::v2::error>>);
    static_assert(std::is_same_v<ce::v2::result<std::string>, std::expected<std::string, ce::v2::error>>);
    static_assert(std::is_same_v<ce::v2::failure, std::unexpected<ce::v2::error>>);
    expect(true);
#else
    // The polyfill build deliberately does not alias std::expected; that backend
    // is what core-result-polyfill covers.
    static_assert(!std::is_same_v<ce::v2::result<int>, ce::v2::result<void>>);
    expect(true);
#endif
  };

  "the alias is transparent to the error type"_test = [] {
    static_assert(std::is_same_v<typename ce::v2::result<int>::error_type, ce::v2::error>);
    static_assert(std::is_same_v<typename ce::v2::result<int>::value_type, int>);
    static_assert(std::is_same_v<typename ce::v2::result<void>::value_type, void>);
    expect(true);
  };
};

const boost::ut::suite<"core-result-polyfill"> core_result_polyfill = [] {
  using namespace boost::ut;

  // The permitted subset must be identical on BOTH backends, or the polyfill
  // stops being a drop-in and removing it later is no longer provably a no-op.
  "the permitted subset exists on every backend"_test = [] {
    static_assert(requires(ce::v2::result<int> r) { r.has_value(); });
    static_assert(requires(ce::v2::result<int> r) { static_cast<bool>(r); });
    static_assert(requires(ce::v2::result<int> r) { *r; });
    static_assert(requires(ce::v2::result<int> r) { r.error(); });
    static_assert(requires(ce::v2::result<std::string> r) { r->size(); });
    static_assert(requires(ce::v2::result<void> r) { r.has_value(); });
    static_assert(requires(ce::v2::result<void> r) { static_cast<bool>(r); });
    static_assert(requires(ce::v2::result<void> r) { r.error(); });
    expect(true);
  };

#if !CE_HAS_EXPECTED
  // value() throws std::bad_expected_access, which SPEC section 9 decision D4
  // forbids; the monadic operations would grow the very surface this type exists
  // to constrain. Their absence here is what makes a banned use a compile error.
  "the banned surface is absent under the polyfill"_test = [] {
    static_assert(!has_value_fn<ce::v2::result<int>>);
    static_assert(!has_value_or<ce::v2::result<int>>);
    static_assert(!has_error_or<ce::v2::result<int>>);
    static_assert(!has_and_then<ce::v2::result<int>>);
    static_assert(!has_or_else<ce::v2::result<int>>);
    static_assert(!has_transform<ce::v2::result<int>>);
    static_assert(!has_value_fn<ce::v2::result<void>>);
    static_assert(!has_and_then<ce::v2::result<void>>);
    expect(true);
  };
#endif

  "the subset behaves the same on whichever backend is built"_test = [] {
    const ce::v2::result<int> ok{7};
    expect(ok.has_value());
    expect(static_cast<bool>(ok));
    expect(*ok == 7);

    const ce::v2::result<int> bad = ce::v2::fail(ce::v2::errc::parse_error, "no digits", "/data");
    expect(!bad.has_value());
    expect(!static_cast<bool>(bad));
    expect(bad.error().code == ce::v2::errc::parse_error);

    const ce::v2::result<void> void_ok{};
    expect(void_ok.has_value());
    const ce::v2::result<void> void_bad = ce::v2::fail(ce::v2::errc::invalid_utf8, "bad continuation byte");
    expect(!void_bad.has_value());
    expect(void_bad.error().code == ce::v2::errc::invalid_utf8);
  };

  // A type whose constructor takes an initializer_list must not be wrapped by it.
  // Brace-initialising the stored value picks that overload, so result<json> held
  // [{"a":1}] where the caller passed {"a":1}. std::expected direct-initialises,
  // so the polyfill has to as well or it is not a drop-in.
  "a value with an initializer_list constructor is stored unchanged"_test = [] {
    struct greedy {
      int tag = 0;
      std::size_t elements = 0;
      greedy() = default;
      explicit greedy(int value) : tag(value) {}
      greedy(std::initializer_list<greedy> list) : elements(list.size()) {}
    };

    const greedy original{7};
    ce::v2::result<greedy> stored = original;

    expect(stored.has_value());
    expect(stored->tag == 7_i) << "the value was rebuilt rather than stored";
    expect(stored->elements == 0_ul) << "an initializer_list constructor was selected";
  };
};

const boost::ut::suite<"core-fail-helper"> core_fail_helper = [] {
  using namespace boost::ut;

  // fail() returns the unexpected carrier rather than a result<T>, which is the
  // whole point: one helper serves every return type with no deduction at the
  // call site and no repetition of T.
  "fail() converts into result<int>"_test = [] {
    const auto parse_thing = [](bool ok) -> ce::v2::result<int> {
      if (!ok) {
        return ce::v2::fail(ce::v2::errc::parse_error, "no digits", "/data");
      }
      return 7;
    };

    const auto ok = parse_thing(true);
    expect(ok.has_value());
    expect(*ok == 7);

    const auto bad = parse_thing(false);
    expect(!bad.has_value());
    expect(bad.error().code == ce::v2::errc::parse_error);
    expect(bad.error().detail == "no digits");
    expect(bad.error().where == "/data");
  };

  "fail() converts into result<std::string>"_test = [] {
    const auto as_string = [](bool ok) -> ce::v2::result<std::string> {
      if (!ok) {
        return ce::v2::fail(ce::v2::errc::out_of_range, "too big");
      }
      return std::string{"fine"};
    };

    expect(as_string(true)->size() == 4U);
    expect(as_string(false).error().code == ce::v2::errc::out_of_range);
  };

  "fail() converts into result<void>"_test = [] {
    const auto do_void = [](bool ok) -> ce::v2::result<void> {
      if (!ok) {
        return ce::v2::fail(ce::v2::errc::invalid_utf8, "bad continuation byte");
      }
      return {};
    };

    expect(do_void(true).has_value());

    const auto bad = do_void(false);
    expect(!bad.has_value());
    expect(bad.error().code == ce::v2::errc::invalid_utf8);
  };

  "the optional fields default to empty"_test = [] {
    const ce::v2::result<int> bare = ce::v2::fail(ce::v2::errc::type_mismatch);
    expect(!bare.has_value());
    expect(bare.error().detail.empty());
    expect(bare.error().where.empty());
  };
};

const boost::ut::suite<"core-attribute-type-aliases"> core_attribute_type_aliases = [] {
  using namespace boost::ut;

  // std::byte rather than char, so a payload never reads as text by accident.
  "binary is a vector of std::byte"_test = [] {
    static_assert(std::is_same_v<ce::v2::binary, std::vector<std::byte>>);
    const ce::v2::binary bytes{std::byte{0x00}, std::byte{0xFF}};
    expect(bytes.size() == 2U);
    expect(bytes[1] == std::byte{0xFF});
  };

  // Three aliases of std::string would make the variant ill-formed and
  // std::get<std::string> uncompilable, so these must be genuinely distinct types.
  "uri, uri_ref and std::string are distinct types"_test = [] {
    static_assert(!std::is_same_v<ce::v2::uri, ce::v2::uri_ref>);
    static_assert(!std::is_same_v<ce::v2::uri, std::string>);
    static_assert(!std::is_same_v<ce::v2::uri_ref, std::string>);
    expect(true);
  };

  "the tagged strings carry their text"_test = [] {
    const ce::v2::uri absolute{"https://example.com/x"};
    expect(absolute.str() == "https://example.com/x");
    expect(absolute.view() == "https://example.com/x"sv);
    expect(absolute.size() == 21U);
    expect(!absolute.empty());
    expect(ce::v2::uri_ref{}.empty());
    expect(ce::v2::uri{"a"} == ce::v2::uri{"a"});
    expect(!(ce::v2::uri{"a"} == ce::v2::uri{"b"}));
  };
};

const boost::ut::suite<"core-attribute-value-variant"> core_attribute_value_variant = [] {
  using namespace boost::ut;

  // Exactly the seven alternatives SPEC section 5.1 lists, and no floating-point
  // one: SPEC section 9 decision D6 makes a float-valued extension a type_mismatch
  // on decode rather than something to round.
  "the variant carries exactly seven alternatives"_test = [] {
    static_assert(std::variant_size_v<ce::v2::attribute_value> == 7);
    static_assert(std::is_same_v<std::variant_alternative_t<0, ce::v2::attribute_value>, bool>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, ce::v2::attribute_value>, std::int32_t>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, ce::v2::attribute_value>, std::string>);
    static_assert(std::is_same_v<std::variant_alternative_t<3, ce::v2::attribute_value>, ce::v2::binary>);
    static_assert(std::is_same_v<std::variant_alternative_t<4, ce::v2::attribute_value>, ce::v2::uri>);
    static_assert(std::is_same_v<std::variant_alternative_t<5, ce::v2::attribute_value>, ce::v2::uri_ref>);
    static_assert(std::is_same_v<std::variant_alternative_t<6, ce::v2::attribute_value>, ce::v2::timestamp>);
    expect(true);
  };

  "there is no floating-point alternative"_test = [] {
    static_assert(!std::is_constructible_v<ce::v2::attribute_value, double>);
    static_assert(!std::is_constructible_v<ce::v2::attribute_value, float>);
    expect(true);
  };

  // uri and uri_ref convert implicitly from std::string, so overload resolution
  // has to prefer the exact match or a plain String attribute would decode as a URI.
  "an exact std::string selects the string alternative"_test = [] {
    expect(std::holds_alternative<std::string>(ce::v2::attribute_value{std::string{"x"}}));
    expect(std::holds_alternative<ce::v2::uri>(ce::v2::attribute_value{ce::v2::uri{"https://example.com"}}));
    expect(std::holds_alternative<ce::v2::uri_ref>(ce::v2::attribute_value{ce::v2::uri_ref{"/rel"}}));
    expect(std::get<ce::v2::uri>(ce::v2::attribute_value{ce::v2::uri{"u"}}).str() == "u");
  };

  "an integer does not decay to bool"_test = [] {
    expect(!std::holds_alternative<bool>(ce::v2::attribute_value{std::int32_t{1}}));
    expect(std::holds_alternative<std::int32_t>(ce::v2::attribute_value{std::int32_t{1}}));
    expect(std::holds_alternative<bool>(ce::v2::attribute_value{true}));
  };
};

const boost::ut::suite<"core-timestamp-representation"> core_timestamp_representation = [] {
  using namespace boost::ut;

  // Chrono types all the way down: the unit is part of the type, so a
  // minutes-for-seconds mix-up is a compile error rather than an instant that is
  // wrong by a factor of sixty.
  "timestamp holds a chrono instant and a chrono offset"_test = [] {
    static_assert(std::is_same_v<decltype(ce::v2::timestamp::utc),
                                 std::chrono::sys_time<std::chrono::nanoseconds>>);
    static_assert(std::is_same_v<decltype(ce::v2::timestamp::offset), std::chrono::minutes>);
    static_assert(std::is_same_v<decltype(ce::v2::timestamp::form), ce::v2::offset_form>);
    // A count, not an integer: the digit count carries its own bound, because a
    // tenth digit is one no nanosecond instant can express (SWR-CORE-0030).
    static_assert(
        std::is_same_v<decltype(ce::v2::timestamp::fractional_digits), ce::v2::fraction_digits>);
    static_assert(std::is_same_v<decltype(ce::v2::fraction_digits{}.count()), std::uint8_t>);
    expect(true);
  };

  // Byte-for-byte round-tripping is why the type stores more than an instant: Z
  // and +00:00 denote the same moment and are both canonical.
  "the spelling is part of the value, not just the instant"_test = [] {
    const auto designator = ce::v2::parse_timestamp("2018-04-05T17:31:00Z"sv);
    const auto numeric = ce::v2::parse_timestamp("2018-04-05T17:31:00+00:00"sv);
    expect(designator.has_value() && numeric.has_value());
    expect(designator->utc == numeric->utc);
    expect(designator->form == ce::v2::offset_form::utc_designator);
    expect(numeric->form == ce::v2::offset_form::numeric);
    expect(!(*designator == *numeric));
  };

  "a timestamp is an aggregate whose instant is required"_test = [] {
    static_assert(std::is_aggregate_v<ce::v2::timestamp>);
    const ce::v2::timestamp epoch{
        .utc = std::chrono::sys_time<std::chrono::nanoseconds>{},
    };
    expect(epoch.offset == std::chrono::minutes{0});
    expect(epoch.form == ce::v2::offset_form::utc_designator);
    expect(epoch.fractional_digits.count() == 0U);
    expect(ce::v2::to_string(epoch) == "1970-01-01T00:00:00Z");
  };
};

const boost::ut::suite<"core-timestamp-parse-ctre"> core_timestamp_parse_ctre = [] {
  using namespace boost::ut;

  "the grammar rejects malformed input"_test = [] {
    expect(rejected(""sv));
    expect(rejected("2018-04-05"sv));               // date only
    expect(rejected("2018-04-05T17:31:00"sv));      // no offset
    expect(rejected("18-04-05T17:31:00Z"sv));       // two-digit year
    expect(rejected("2018-04-05T17:31:00Z "sv));    // trailing space
    expect(rejected(" 2018-04-05T17:31:00Z"sv));    // leading space
    expect(rejected("2018-04-05T17:31:00ZZ"sv));
    expect(rejected("2018-04-05T17:31:00.Z"sv));            // empty fraction
    expect(rejected("2018-04-05T17:31:00.1234567890Z"sv));  // 10 fractional digits
  };

  "the grammar rejects impossible field values"_test = [] {
    expect(rejected("2018-13-05T17:31:00Z"sv));   // month 13
    expect(rejected("2018-04-31T17:31:00Z"sv));   // April has 30 days
    expect(rejected("2019-02-29T00:00:00Z"sv));   // not a leap year
    expect(rejected("2018-04-05T24:31:00Z"sv));   // hour 24
    expect(rejected("2018-04-05T17:61:00Z"sv));   // minute 61
    expect(rejected("2018-04-05T17:31:61Z"sv));   // second 61
    expect(rejected("2018-04-05T17:31:00+24:00"sv));  // offset hour 24
  };

  // sys_time<nanoseconds> counts nanoseconds in an int64 and so spans only about
  // 1678 to 2262, while RFC 3339 admits any four-digit year. Before the range
  // check existed, 9999-12-31 parsed cleanly and came back as 1816-03-30, so this
  // must be a typed error and never a silent wrap.
  "an unrepresentable year is out_of_range, not a silent wrap"_test = [] {
    const auto far = ce::v2::parse_timestamp("9999-12-31T23:59:59Z"sv);
    expect(!far.has_value());
    expect(!far.has_value() && far.error().code == ce::v2::errc::out_of_range);

    const auto ancient = ce::v2::parse_timestamp("1000-01-01T00:00:00Z"sv);
    expect(!ancient.has_value());
    expect(!ancient.has_value() && ancient.error().code == ce::v2::errc::out_of_range);
  };

  "a malformed date-time reports invalid_timestamp and names the input"_test = [] {
    const auto bad = ce::v2::parse_timestamp("not a timestamp"sv);
    expect(!bad.has_value());
    expect(!bad.has_value() && bad.error().code == ce::v2::errc::invalid_timestamp);
    expect(!bad.has_value() && bad.error().where == "not a timestamp");
  };

  "offsets shift the instant"_test = [] {
    const auto zulu = ce::v2::parse_timestamp("2018-04-05T17:31:00Z"sv);
    const auto plus = ce::v2::parse_timestamp("2018-04-05T18:31:00+01:00"sv);
    expect(zulu.has_value() && plus.has_value());
    expect(zulu->utc == plus->utc);
    // ...but they are not equal, because the spelling differs.
    expect(!(*zulu == *plus));
  };

  // ".5" is 500ms because the first place IS 100ms, not because a bare integer was
  // scaled by whichever power of ten happened to be written down.
  "a fraction scales by place value"_test = [] {
    const auto half = ce::v2::parse_timestamp("2018-04-05T17:31:00.5Z"sv);
    const auto full = ce::v2::parse_timestamp("2018-04-05T17:31:00.500000000Z"sv);
    expect(half.has_value() && full.has_value());
    expect(half->utc == full->utc);
    expect(half->fractional_digits == 1U);
    expect(full->fractional_digits == 9U);
  };
};

const boost::ut::suite<"core-timestamp-roundtrip"> core_timestamp_roundtrip = [] {
  using namespace boost::ut;

  "canonical input round-trips byte for byte"_test = [] {
    expect(round_trips("2018-04-05T17:31:00Z"sv));
    expect(round_trips("2020-02-29T00:00:00Z"sv));  // leap day
    expect(round_trips("1970-01-01T00:00:00Z"sv));  // epoch
    expect(round_trips("2262-04-01T00:00:00Z"sv));  // inside the nanosecond range
    expect(round_trips("1700-01-01T00:00:00Z"sv));  // well before the epoch
  };

  // Trailing zeros carry no information about the instant, so only storing the
  // digit count keeps them from being dropped on the way out.
  "fractional precision survives"_test = [] {
    expect(round_trips("2018-04-05T17:31:00.000Z"sv));
    expect(round_trips("2018-04-05T17:31:00.123456789Z"sv));
    expect(round_trips("2018-04-05T17:31:00.5Z"sv));
  };

  "the offset spelling survives"_test = [] {
    expect(round_trips("2018-04-05T17:31:00+00:00"sv));  // +00:00 is NOT Z
    expect(round_trips("2018-04-05T17:31:00-07:00"sv));
    expect(round_trips("2018-04-05T17:31:00+05:30"sv));  // half-hour offset
  };

  // The nine-digit field renders whole. A count above nine used to be settable
  // and divided a place value that had reached zero; it is now unconstructible,
  // and timestamp_test holds that. What is left to check here is the rendering.
  "every digit the instant carries is rendered"_test = [] {
    expect(round_trips("2018-04-05T17:31:00.123456789Z"sv));
    expect(round_trips("2018-04-05T17:31:00.000000001Z"sv));
  };

  // Truncation keeps the leading digits, so a shorter count is a prefix of the
  // full nanosecond field rather than a different number.
  "a shorter digit count truncates rather than rounds"_test = [] {
    const auto parsed = ce::v2::parse_timestamp("2018-04-05T17:31:00.987654321Z"sv);
    expect(parsed.has_value());
    if (!parsed) {
      return;
    }
    auto three = *parsed;
    three.fractional_digits = 3;
    expect(ce::v2::to_string(three) == "2018-04-05T17:31:00.987Z");
  };
};

const boost::ut::suite<"core-timestamp-lenient-parse"> core_timestamp_lenient_parse = [] {
  using namespace boost::ut;

  // Strict on produce, tolerant on consume (SPEC section 5.1). These are accepted,
  // but they are not required to round-trip: to_string always emits canonical form.
  "lowercase t and z are accepted on consume"_test = [] {
    expect(ce::v2::parse_timestamp("2018-04-05t17:31:00z"sv).has_value());
    expect(ce::v2::parse_timestamp("2018-04-05T17:31:00z"sv).has_value());
    expect(ce::v2::parse_timestamp("2018-04-05t17:31:00Z"sv).has_value());
  };

  "a leap second is accepted rather than becoming a decode failure"_test = [] {
    expect(ce::v2::parse_timestamp("2018-04-05T23:59:60Z"sv).has_value());
    expect(ce::v2::parse_timestamp("2016-12-31T23:59:60Z"sv).has_value());
  };

  // sys_time has no representation for a leap second, so it folds onto the
  // following second and deliberately does not round-trip. Asserting the fold is
  // the only way that stays stated rather than assumed.
  "a leap second folds onto the next second"_test = [] {
    const auto leap = ce::v2::parse_timestamp("2016-12-31T23:59:60Z"sv);
    expect(leap.has_value());
    expect(leap.has_value() && ce::v2::to_string(*leap) == "2017-01-01T00:00:00Z");
  };

  "leniency does not extend to canonicalising the output"_test = [] {
    const auto lower = ce::v2::parse_timestamp("2018-04-05t17:31:00z"sv);
    expect(lower.has_value());
    expect(lower.has_value() && ce::v2::to_string(*lower) == "2018-04-05T17:31:00Z");
  };
};

const boost::ut::suite<"core-timestamp-no-chrono-parse"> core_timestamp_no_chrono_parse = [] {
  using namespace boost::ut;

  // The prohibition on std::chrono::parse and std::regex is a property of the
  // header's source text, not of any value this suite can observe, so there is no
  // honest run-time assertion to make. What IS observable is the consequence: the
  // single CTRE pattern is the only parse path, and it is constexpr, which
  // std::chrono::parse is not. A parse that runs in a constant expression could
  // not have gone through a stream-based facility.
  "the parse path is the constexpr CTRE pattern, not a stream facility"_test = [] {
    static_assert(ctre::match<ce::v2::detail::rfc3339_pattern>("2018-04-05T17:31:00Z"sv));
    static_assert(!ctre::match<ce::v2::detail::rfc3339_pattern>("2018-04-05"sv));
    expect(true);
  };

  // Every rule the parser applies beyond the pattern is arithmetic on chrono
  // durations, which is also why the lenient cases above are accepted here rather
  // than in a second pass over a different grammar.
  "there is exactly one definition of the grammar"_test = [] {
    // A lowercase t and a seconds field of 60 are matched by the same pattern that
    // matches canonical input; a second parser would have to be kept in step.
    static_assert(ctre::match<ce::v2::detail::rfc3339_pattern>("2018-04-05t17:31:00z"sv));
    static_assert(ctre::match<ce::v2::detail::rfc3339_pattern>("2016-12-31T23:59:60Z"sv));
    expect(true);
  };
};

const boost::ut::suite<"core-data-t-variant"> core_data_t_variant = [] {
  using namespace boost::ut;

  "data is absent, text, bytes or pre-serialized JSON"_test = [] {
    static_assert(std::variant_size_v<ce::v2::data_t> == 4);
    static_assert(std::is_same_v<std::variant_alternative_t<0, ce::v2::data_t>, std::monostate>);
    static_assert(std::is_same_v<std::variant_alternative_t<1, ce::v2::data_t>, std::string>);
    static_assert(std::is_same_v<std::variant_alternative_t<2, ce::v2::data_t>, ce::v2::binary>);
    static_assert(std::is_same_v<std::variant_alternative_t<3, ce::v2::data_t>, ce::v2::json_text>);
    expect(true);
  };

  // monostate first, so a default-constructed event has no data rather than an
  // empty string, which is a different thing on the wire.
  "data defaults to absent"_test = [] {
    const auto event = good_event();
    expect(std::holds_alternative<std::monostate>(event.data()));
    expect(event.data().index() == 0U);
  };

  "each alternative is reachable and keeps its bytes"_test = [] {
    const auto text = good_event({.data = std::string{"plain text"}});
    expect(std::holds_alternative<std::string>(text.data()));
    expect(std::get<std::string>(text.data()) == "plain text");

    const auto bytes =
        good_event({.data = ce::v2::binary{std::byte{0x00}, std::byte{0x80}, std::byte{0xFF}}});
    expect(std::holds_alternative<ce::v2::binary>(bytes.data()));
    expect(std::get<ce::v2::binary>(bytes.data()).size() == 3U);
    expect(std::get<ce::v2::binary>(bytes.data())[1] == std::byte{0x80});

    const auto json = good_event({.data = ce::v2::json_text{.raw = R"({"a":1})"}});
    expect(std::holds_alternative<ce::v2::json_text>(json.data()));
  };

  "data participates in event equality"_test = [] {
    expect(good_event() == good_event());
    expect(!(good_event({.data = std::string{"x"}}) == good_event()));
    expect(good_event({.data = std::string{"x"}}) == good_event({.data = std::string{"x"}}));
  };

  // The one attribute that changes after construction, and it changes together
  // with the media type describing it (SWR-CORE-0015).
  "set_data replaces the payload and its media type together"_test = [] {
    auto event = good_event({.datacontenttype = "text/plain"_mediatype});
    event.set_data(ce::v2::json_text{.raw = "1"}, "application/json"_mediatype);
    expect(event == good_event({.datacontenttype = "application/json"_mediatype,
                                .data = ce::v2::json_text{.raw = "1"}}));

    event.set_data(ce::v2::binary{std::byte{0x01}}, std::nullopt);
    expect(!event.datacontenttype().has_value());
    expect(std::holds_alternative<ce::v2::binary>(event.data()));
  };
};

const boost::ut::suite<"core-json-text-codec-free"> core_json_text_codec_free = [] {
  using namespace boost::ut;

  // json_text is what keeps core free of any codec: it stores the bytes and never
  // parses or validates them. The format layer is the only thing that turns this
  // into a JSON value (ADR-0004).
  "json_text is an opaque string, with no parsed representation"_test = [] {
    static_assert(std::is_aggregate_v<ce::v2::json_text>);
    static_assert(std::is_same_v<decltype(ce::v2::json_text::raw), std::string>);
    expect(true);
  };

  "the bytes are stored verbatim"_test = [] {
    const ce::v2::json_text json{.raw = R"({"a":1})"};
    expect(json.raw == R"({"a":1})");

    // Whitespace and key order are part of the bytes, because nothing here
    // re-serializes them.
    const ce::v2::json_text spaced{.raw = R"({ "b" : 2 ,  "a" : 1 })"};
    expect(spaced.raw == R"({ "b" : 2 ,  "a" : 1 })");
  };

  // Core does not know what valid JSON is, so it cannot and must not reject this.
  // A decoder is where malformed input becomes a parse_error.
  "core stores malformed JSON without complaint"_test = [] {
    const auto event = good_event({.data = ce::v2::json_text{.raw = "{not json at all"}});
    expect(std::get<ce::v2::json_text>(event.data()).raw == "{not json at all");
  };

  "json_text compares by its bytes"_test = [] {
    expect(ce::v2::json_text{.raw = "1"} == ce::v2::json_text{.raw = "1"});
    expect(!(ce::v2::json_text{.raw = "1"} == ce::v2::json_text{.raw = " 1"}));
  };
};

const boost::ut::suite<"core-event-required-attributes"> core_event_required_attributes = [] {
  using namespace boost::ut;

  // No aggregate and no default: the only ways in take attributes that have
  // already passed their rule, so there is no event to build that CloudEvents
  // forbids and nothing left to check once it exists.
  "the event is built from validated attributes, never as an aggregate"_test = [] {
    static_assert(!std::is_aggregate_v<ce::v2::event>);
    static_assert(!std::is_default_constructible_v<ce::v2::event>);
    static_assert(std::is_constructible_v<ce::v2::event, ce::v2::id, ce::v2::source, ce::v2::type>);
    static_assert(
        std::is_constructible_v<ce::v2::event, ce::v2::id, ce::v2::source, ce::v2::type, ce::v2::event::options>);
    static_assert(!std::is_constructible_v<ce::v2::event, std::string, std::string, std::string>);
    static_assert(!std::is_constructible_v<ce::v2::event, const char*, const char*, const char*>);

    const ce::v2::event event{"1"_id, "/spec/test"_source, "com.example.thing"_type};
    expect(event.id().view() == "1"sv);
    expect(event.source().view() == "/spec/test"sv);
    expect(event.type().view() == "com.example.thing"sv);
  };

  "specversion is the only version this SDK implements"_test = [] {
    static_assert(std::is_same_v<decltype(good_event().specversion()), ce::v2::spec_version>);
    expect(good_event().specversion().view() == "1.0"sv);
  };

  "each required attribute is its own type, not a plain string"_test = [] {
    using event_ref = const ce::v2::event&;
    static_assert(std::is_same_v<decltype(std::declval<event_ref>().id()), const ce::v2::id&>);
    static_assert(std::is_same_v<decltype(std::declval<event_ref>().source()), const ce::v2::source&>);
    static_assert(std::is_same_v<decltype(std::declval<event_ref>().type()), const ce::v2::type&>);
    static_assert(!std::is_same_v<ce::v2::id, ce::v2::type>);
    static_assert(!std::is_same_v<ce::v2::source, std::string>);
    expect(true);
  };

  "equality is value equality across the whole event"_test = [] {
    expect(good_event() == good_event());
    expect(!(good_event() == good_event({.subject = "x"_subject})));
  };
};

const boost::ut::suite<"core-event-optional-attributes"> core_event_optional_attributes = [] {
  using namespace boost::ut;

  // Optional in the spec means optional in the type: std::optional distinguishes
  // absent from present-and-empty, which an empty string could not.
  "the optional attributes are std::optional, behind const accessors"_test = [] {
    using event_ref = const ce::v2::event&;
    static_assert(std::is_same_v<decltype(std::declval<event_ref>().datacontenttype()),
                                 const std::optional<ce::v2::datacontenttype>&>);
    static_assert(std::is_same_v<decltype(std::declval<event_ref>().dataschema()),
                                 const std::optional<ce::v2::dataschema>&>);
    static_assert(std::is_same_v<decltype(std::declval<event_ref>().subject()),
                                 const std::optional<ce::v2::subject>&>);
    static_assert(std::is_same_v<decltype(std::declval<event_ref>().time()),
                                 const std::optional<ce::v2::timestamp>&>);
    static_assert(
        std::is_same_v<decltype(std::declval<event_ref>().data()), const ce::v2::data_t&>);
    static_assert(std::is_same_v<decltype(std::declval<event_ref>().extensions()),
                                 const ce::v2::event::extension_map&>);
    static_assert(std::is_same_v<ce::v2::event::extension_map,
                                 std::map<ce::v2::extension_name, ce::v2::attribute_value, std::less<>>>);
    expect(true);
  };

  "they default to absent"_test = [] {
    const auto event = good_event();
    expect(!event.datacontenttype().has_value());
    expect(!event.dataschema().has_value());
    expect(!event.subject().has_value());
    expect(!event.time().has_value());
  };

  "a present optional attribute is carried"_test = [] {
    const auto event = good_event({
        .datacontenttype = "application/json"_mediatype,
        .dataschema = "https://example.com/schema.json"_dataschema,
        .subject = "ok"_subject,
    });
    expect(event.datacontenttype().has_value() &&
           event.datacontenttype()->view() == "application/json"sv);
    expect(event.dataschema().has_value() &&
           event.dataschema()->view() == "https://example.com/schema.json"sv);
    expect(event.subject().has_value() && event.subject()->view() == "ok"sv);
  };

  // Timestamps are chrono-typed all the way into the event, so time is an instant
  // rather than a string that has to be re-parsed by every reader.
  "time is a parsed timestamp, not text"_test = [] {
    const auto parsed = ce::v2::parse_timestamp("2018-04-05T17:31:00Z"sv);
    expect(parsed.has_value());
    if (!parsed) {
      return;
    }
    const auto event = good_event({.time = *parsed});
    expect(event.time().has_value() &&
           ce::v2::to_string(*event.time()) == "2018-04-05T17:31:00Z");
  };
};

const boost::ut::suite<"core-extension-accessors"> core_extension_accessors = [] {
  using namespace boost::ut;

  "set_extension stores a value that extension() finds"_test = [] {
    auto event = good_event();
    event.set_extension("traceparent"_ext, std::string{"00-x-y-01"});
    expect(event.extension("traceparent") != nullptr);
    expect(std::get<std::string>(*event.extension("traceparent")) == "00-x-y-01");
  };

  // The name has already passed its rule, so there is no refusal to discard.
  "set_extension has no failure path"_test = [] {
    static_assert(std::is_void_v<decltype(std::declval<ce::v2::event&>().set_extension(
                      "a"_ext, ce::v2::attribute_value{true}))>);
    expect(true);
  };

  "remove_extension reports whether the name was present"_test = [] {
    auto event = good_event();
    event.set_extension("seq"_ext, std::int32_t{1});
    expect(event.remove_extension("seq"));
    expect(event.extension("seq") == nullptr);
    expect(!event.remove_extension("seq"));
    expect(event.extensions().empty());
  };

  // A pointer rather than an optional, so absence costs nothing and the caller can
  // distinguish it from a present-but-empty value.
  "an absent extension is a null pointer"_test = [] {
    const auto event = good_event();
    expect(event.extension("absent") == nullptr);
    expect(event.extension("") == nullptr);
  };

  // Strict on produce: an invalid or reserved name is refused where the name is
  // made, rather than discovered by a peer at decode time. It never reaches the
  // event, which is why set_extension has nothing left to refuse.
  "a name the spec forbids cannot reach set_extension"_test = [] {
    expect(refused(ce::v2::extension_name::make("Trace-Parent"sv), ce::v2::errc::invalid_attribute_name,
                   "Trace-Parent"));
    expect(refused(ce::v2::extension_name::make("id"sv), ce::v2::errc::reserved_attribute_name, "id"));
  };

  // The map has a transparent comparator, so a string_view key looks up without
  // materialising a key on every call.
  "lookup is transparent and does not require building a key"_test = [] {
    auto event = good_event();
    event.set_extension("seq"_ext, std::int32_t{7});
    expect(event.extension(std::string_view{"seq"}) != nullptr);
    expect(std::get<std::int32_t>(*event.extension("seq")) == 7);
    static_assert(requires { typename ce::v2::event::extension_map::key_compare::is_transparent; });
  };

  "setting the same name twice replaces the value"_test = [] {
    auto event = good_event();
    event.set_extension("seq"_ext, std::int32_t{1});
    event.set_extension("seq"_ext, std::int32_t{2});
    expect(event.extensions().size() == 1U);
    expect(std::get<std::int32_t>(*event.extension("seq")) == 2);
  };
};

const boost::ut::suite<"core-required-attributes-refuse-empty"> core_required_refuse_empty = [] {
  using namespace boost::ut;

  "a minimal event with all three required attributes is constructed"_test = [] {
    expect(good_event().id().view() == "1"sv);
  };

  // Each required attribute is its own error site, so the report names which one
  // is missing rather than saying only that something is.
  "each empty required attribute is its own error"_test = [] {
    expect(refused(ce::v2::id::make(""sv), ce::v2::errc::missing_required_attribute, "id"));
    expect(refused(ce::v2::source::make(""sv), ce::v2::errc::missing_required_attribute, "source"));
    expect(refused(ce::v2::type::make(""sv), ce::v2::errc::missing_required_attribute, "type"));
  };

  "a non-empty required attribute is made"_test = [] {
    expect(ce::v2::id::make("1"sv).has_value());
    expect(ce::v2::source::make("/spec/test"sv).has_value());
    expect(ce::v2::type::make("com.example.thing"sv).has_value());
  };
};

const boost::ut::suite<"core-specversion-is-1-0-only"> core_specversion_is_1_0_only = [] {
  using namespace boost::ut;

  // SPEC section 9 decision D5: this SDK implements 1.0 only, and says so with its
  // own error code rather than reporting a generic invalid value.
  "a specversion other than 1.0 is unsupported_spec_version"_test = [] {
    for (const auto version : {"0.3"sv, "1.1"sv, "2.0"sv, ""sv, "1.0.0"sv}) {
      expect(refused(ce::v2::spec_version::make(version), ce::v2::errc::unsupported_spec_version,
                     "specversion"))
          << version;
    }
  };

  // Not a string at all on the produce side: a version that does not exist has
  // no spelling to be given.
  "spec_version has a single value"_test = [] {
    static_assert(std::is_default_constructible_v<ce::v2::spec_version>);
    static_assert(ce::v2::spec_version{}.view() == "1.0");
    expect(ce::v2::spec_version{} == ce::v2::spec_version{});
  };

  "1.0 is accepted"_test = [] {
    const auto version = ce::v2::spec_version::make("1.0"sv);
    expect(version.has_value());
    expect(version.has_value() && *version == ce::v2::spec_version{});
  };
};

const boost::ut::suite<"core-optional-attributes-refuse-empty"> core_optional_refuse_empty = [] {
  using namespace boost::ut;

  // An optional attribute may be absent, but the spec requires it to be non-empty
  // when present: an empty string means a producer set it by mistake.
  "a present-but-empty optional attribute is invalid_attribute_value"_test = [] {
    expect(refused(ce::v2::subject::make(""sv), ce::v2::errc::invalid_attribute_value, "subject"));
    expect(refused(ce::v2::dataschema::make(""sv), ce::v2::errc::invalid_attribute_value, "dataschema"));
    expect(refused(ce::v2::datacontenttype::make(""sv), ce::v2::errc::invalid_attribute_value,
                   "datacontenttype"));
  };

  "absent is not the same as empty, and absent is fine"_test = [] {
    const auto event = good_event({
        .datacontenttype = std::nullopt,
        .dataschema = std::nullopt,
        .subject = std::nullopt,
    });
    expect(event == good_event());
  };

  "a non-empty optional attribute is accepted"_test = [] {
    expect(ce::v2::subject::make("ok"sv).has_value());
    expect(ce::v2::dataschema::make("https://example.com/s"sv).has_value());
    expect(ce::v2::datacontenttype::make("application/json"sv).has_value());
  };

  // datacontenttype carries a second rule: it has to be a media type at all.
  "a datacontenttype that is not a media type is invalid_content_type"_test = [] {
    expect(refused(ce::v2::datacontenttype::make("not a media type"sv),
                   ce::v2::errc::invalid_content_type, "datacontenttype"));
  };
};

const boost::ut::suite<"core-extension-names-refuse-invalid"> core_extension_names_refuse = [] {
  using namespace boost::ut;

  // An extension name has no fixed identity to report, so the refusal names the
  // text that was offered.
  "an extension name that is not [a-z0-9]+ is invalid_attribute_name"_test = [] {
    expect(refused(ce::v2::extension_name::make("Trace-Parent"sv), ce::v2::errc::invalid_attribute_name,
                   "Trace-Parent"));
  };

  "an extension that shadows a context attribute is reserved_attribute_name"_test = [] {
    expect(refused(ce::v2::extension_name::make("id"sv), ce::v2::errc::reserved_attribute_name, "id"));
  };

  "well-formed extension names are made"_test = [] {
    auto event = good_event();
    event.set_extension("traceparent"_ext, std::string{"00-x-y-01"});
    event.set_extension("seq"_ext, std::int32_t{7});
    event.set_extension("a1"_ext, true);
    expect(event.extensions().size() == 3U);
    expect(ce::v2::extension_name::make("traceparent"sv).has_value());
  };
};

/// An extension name of `length` characters, made at run time.
[[nodiscard]] auto name_of_length(std::size_t length) -> ce::v2::extension_name {
  auto made = ce::v2::extension_name::make(std::string(length, 'a'));
  boost::ut::expect(made.has_value());
  return made.has_value() ? *made : ce::v2::extension_name{"fallback"_ext};
}

const boost::ut::suite<"core-lint-long-extension-name"> core_lint_long_extension_name = [] {
  using namespace boost::ut;

  // The 20-character limit is a SHOULD in the core spec, so refusing the name
  // would reject events the spec permits. It surfaces as a warning.
  "a long extension name is a lint warning, not a refusal"_test = [] {
    constexpr std::size_t long_length = 25;
    auto event = good_event();
    event.set_extension(name_of_length(long_length), std::string{"v"});

    const auto warnings = event.lint();
    expect(warnings.size() == 1U);
    expect(warnings.size() == 1U && warnings[0].attribute == std::string(long_length, 'a'));
    expect(warnings.size() == 1U && !warnings[0].message.empty());
  };

  "a conforming event lints clean"_test = [] {
    auto event = good_event();
    expect(event.lint().empty());
    event.set_extension("traceparent"_ext, std::string{"x"});
    expect(event.lint().empty());
  };

  // Exactly 20 is within the recommendation; 21 is the first one that is not.
  "the boundary is at twenty characters"_test = [] {
    constexpr std::size_t at_limit_length = 20;
    auto at_limit = good_event();
    at_limit.set_extension(name_of_length(at_limit_length), std::string{"v"});
    expect(at_limit.lint().empty());

    auto over_limit = good_event();
    over_limit.set_extension(name_of_length(at_limit_length + 1), std::string{"v"});
    expect(over_limit.lint().size() == 1U);
  };
};

const boost::ut::suite<"core-valid-attribute-name"> core_valid_attribute_name = [] {
  using namespace boost::ut;

  // A constexpr validator that is only usable at run time has already broken its
  // contract, so these are static_asserts rather than expects (SPEC section 6).
  "valid_attribute_name is usable in a constant expression"_test = [] {
    static_assert(ce::v2::valid_attribute_name("traceparent"));
    static_assert(ce::v2::valid_attribute_name("a1"));
    static_assert(ce::v2::valid_attribute_name("0"));
    static_assert(ce::v2::valid_attribute_name("abcdefghijklmnopqrstuvwxyz0123456789"));
    expect(true);
  };

  "the name must be non-empty lowercase alphanumerics"_test = [] {
    static_assert(!ce::v2::valid_attribute_name(""));
    static_assert(!ce::v2::valid_attribute_name("Trace"));         // uppercase
    static_assert(!ce::v2::valid_attribute_name("trace-parent"));  // hyphen
    static_assert(!ce::v2::valid_attribute_name("trace_parent"));  // underscore
    static_assert(!ce::v2::valid_attribute_name("trace parent"));  // space
    static_assert(!ce::v2::valid_attribute_name("trace.parent"));  // dot
    expect(true);
  };

  // Length is deliberately not checked here: the 20-character limit is a SHOULD,
  // so it belongs in lint(), not in the name grammar (SWR-CORE-0021).
  "length is not part of the name grammar"_test = [] {
    static_assert(ce::v2::valid_attribute_name("aaaaaaaaaaaaaaaaaaaaaaaaa"));
    expect(true);
  };
};

const boost::ut::suite<"core-reserved-name"> core_reserved_name = [] {
  using namespace boost::ut;

  "every context attribute name is reserved, in a constant expression"_test = [] {
    static_assert(ce::v2::reserved_name("id"));
    static_assert(ce::v2::reserved_name("source"));
    static_assert(ce::v2::reserved_name("specversion"));
    static_assert(ce::v2::reserved_name("type"));
    static_assert(ce::v2::reserved_name("datacontenttype"));
    static_assert(ce::v2::reserved_name("dataschema"));
    static_assert(ce::v2::reserved_name("subject"));
    static_assert(ce::v2::reserved_name("time"));
    static_assert(ce::v2::reserved_name("data"));
    // data_base64 is reserved although it is a JSON-format name rather than a
    // context attribute: an extension called data_base64 would collide on the wire.
    static_assert(ce::v2::reserved_name("data_base64"));
    expect(true);
  };

  "an ordinary extension name is not reserved"_test = [] {
    static_assert(!ce::v2::reserved_name("traceparent"));
    static_assert(!ce::v2::reserved_name(""));
    static_assert(!ce::v2::reserved_name("ids"));
    static_assert(!ce::v2::reserved_name("i"));
    // The comparison is exact, not case-insensitive and not a prefix match.
    static_assert(!ce::v2::reserved_name("ID"));
    static_assert(!ce::v2::reserved_name("timestamp"));
    expect(true);
  };
};

const boost::ut::suite<"core-is-json-content-type"> core_is_json_content_type = [] {
  using namespace boost::ut;

  "a JSON media type is recognised in a constant expression"_test = [] {
    static_assert(ce::v2::is_json_content_type("application/json"));
    static_assert(ce::v2::is_json_content_type("text/json"));
    static_assert(ce::v2::is_json_content_type("application/cloudevents+json"));
    expect(true);
  };

  // Media types are case-insensitive, and parameters are allowed after the
  // subtype, so a charset must not stop a payload being read as JSON.
  "the match ignores case and tolerates parameters"_test = [] {
    static_assert(ce::v2::is_json_content_type("APPLICATION/JSON"));
    static_assert(ce::v2::is_json_content_type("Application/CloudEvents+JSON"));
    static_assert(ce::v2::is_json_content_type("application/json; charset=utf-8"));
    expect(true);
  };

  "a non-JSON or malformed media type is not JSON"_test = [] {
    static_assert(!ce::v2::is_json_content_type("application/xml"));
    static_assert(!ce::v2::is_json_content_type("text/plain"));
    static_assert(!ce::v2::is_json_content_type("notamediatype"));
    static_assert(!ce::v2::is_json_content_type(""));
    expect(true);
  };

  // "+json" is a structured-syntax suffix, so it only counts after something
  // else; a bare ends_with would wrongly accept a subtype that merely ends in
  // those characters.
  "the suffix rule does not degrade into ends_with"_test = [] {
    static_assert(!ce::v2::is_json_content_type("application/jsonx"));
    static_assert(!ce::v2::is_json_content_type("application/notjson"));
    expect(true);
  };
};

const boost::ut::suite<"core-source-non-empty-only"> core_source_non_empty_only = [] {
  using namespace boost::ut;

  // SPEC section 5.1 makes full RFC 3986 validation explicitly not required: a
  // receiver that rejects a URI its peer considers valid is worse than one that
  // passes it along. Non-emptiness is the whole rule.
  "an empty source is the only source that fails"_test = [] {
    expect(refused(ce::v2::source::make(""sv), ce::v2::errc::missing_required_attribute, "source"));
  };

  "anything non-empty is accepted, however unlike a URI it looks"_test = [] {
    for (const auto text : {"/spec/test"sv, "https://example.com/x"sv, "urn:uuid:1234"sv,
                            "my-source"sv, "not a uri at all %%% \t"sv, " "sv,
                            "../relative/../ref"sv}) {
      expect(ce::v2::source::make(text).has_value()) << text;
    }
  };

  "source keeps its text verbatim"_test = [] {
    const auto made = ce::v2::source::make("not a uri at all %%%"sv);
    expect(made.has_value());
    if (!made) {
      return;
    }
    const ce::v2::event event{"1"_id, *made, "com.example.thing"_type};
    expect(event.source().view() == "not a uri at all %%%"sv);
  };
};

const boost::ut::suite<"core-event-model"> core_event_model = [] {
  using namespace boost::ut;

  // The system-level view: a fully populated event is built in one expression,
  // and there is no separate check to remember afterwards.
  "a fully populated event is constructed"_test = [] {
    const auto parsed = ce::v2::parse_timestamp("2018-04-05T17:31:00Z"sv);
    expect(parsed.has_value());
    if (!parsed) {
      return;
    }
    const auto event = good_event({
        .datacontenttype = "application/json"_mediatype,
        .dataschema = "https://example.com/schema.json"_dataschema,
        .subject = "orders/42"_subject,
        .time = *parsed,
        .extensions = {{"traceparent"_ext, std::string{"00-x-y-01"}},
                       {"seq"_ext, std::int32_t{7}}},
        .data = ce::v2::json_text{.raw = R"({"amount":1})"},
    });
    expect(event.extensions().size() == 2U);
    expect(event.lint().empty());
  };

  // Every category of refusal the model holds, side by side. Listing them together
  // is what makes it visible that each rule lives with the value it constrains,
  // rather than in one of several places a rule might be checked.
  "each rule is refused where the value is made"_test = [] {
    expect(refused(ce::v2::spec_version::make("0.3"sv), ce::v2::errc::unsupported_spec_version,
                   "specversion"));
    expect(refused(ce::v2::id::make(""sv), ce::v2::errc::missing_required_attribute, "id"));
    expect(refused(ce::v2::subject::make(""sv), ce::v2::errc::invalid_attribute_value, "subject"));
    expect(refused(ce::v2::datacontenttype::make("not a media type"sv),
                   ce::v2::errc::invalid_content_type, "datacontenttype"));
    expect(refused(ce::v2::extension_name::make("Bad-Name"sv), ce::v2::errc::invalid_attribute_name,
                   "Bad-Name"));
    expect(refused(ce::v2::extension_name::make("time"sv), ce::v2::errc::reserved_attribute_name,
                   "time"));
  };

  // Round-tripping an event through the model must not change it, which is the
  // property every format layer above the core will rely on.
  "an event survives a copy unchanged"_test = [] {
    const auto parsed = ce::v2::parse_timestamp("2018-04-05T17:31:00.500Z"sv);
    expect(parsed.has_value());
    if (!parsed) {
      return;
    }
    auto event = good_event({.subject = "orders/42"_subject, .time = *parsed});
    event.set_extension("seq"_ext, std::int32_t{7});

    const ce::v2::event copy = event;
    expect(copy == event);
    expect(copy.time().has_value() &&
           ce::v2::to_string(*copy.time()) == "2018-04-05T17:31:00.500Z");
  };

  "a warning is not an error"_test = [] {
    constexpr std::size_t long_length = 25;
    auto event = good_event();
    event.set_extension(name_of_length(long_length), std::string{"v"});
    expect(event.lint().size() == 1U);
  };
};

}  // namespace

int main() {}
