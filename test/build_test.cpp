#include <boost/ut.hpp>

#include <cloudevents/attributes.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/expected_polyfill.hpp>
#include <cloudevents/detail/timestamp.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <concepts>
#include <type_traits>

// Build-level requirements whose subject is the core, so they cannot live in
// config_test.cpp, which compiles against detail/config.hpp alone.

namespace {

// ---------------------------------------------------------------------------
// Detectors for the permitted result<T> subset.
// ---------------------------------------------------------------------------
// Templates on purpose: a requires-expression over a concrete type hard-errors
// instead of evaluating to false.

template <class R>
concept has_has_value = requires(const R& r) {
  { r.has_value() } -> std::same_as<bool>;
};
template <class R>
concept has_bool_conversion = requires(const R& r) {
  { static_cast<bool>(r) } -> std::same_as<bool>;
};
template <class R>
concept has_indirection = requires(R& r) { *r; };
template <class R>
concept has_arrow = requires(R& r) { r.operator->(); };
template <class R>
concept has_error_fn = requires(const R& r) {
  { r.error() } -> std::same_as<const ce::error&>;
};

// spec: SWR-BUILD-0004
const boost::ut::suite<"config-polyfill-parity"> config_polyfill_parity = [] {
  using namespace boost::ut;

  // Parity here does NOT mean the two surfaces are equal. ADR-0002 makes the
  // polyfill deliberately SMALLER than std::expected -- value(), value_or, the
  // monadic operations and the comparisons are absent so that using one is a hard
  // error in the polyfill build. What must match is the permitted subset, because
  // that is what call sites are written against, and it is what makes deleting the
  // polyfill at the C++23 floor a provable no-op.
  //
  // The polyfill is named directly so it is checked on EVERY preset, not only the
  // one where it happens to back result<T>; ce::result covers whichever backend
  // this build selected. Between gcc-cxx23 (std::expected) and polyfill-cxx23 both
  // backends are therefore exercised through the same assertions.

  using poly_int = ce::v1::detail::poly::expected<int, ce::error>;
  using poly_void = ce::v1::detail::poly::expected<void, ce::error>;
  using result_int = ce::result<int>;
  using result_void = ce::result<void>;

  "the permitted subset is present on both backends"_test = [] {
    static_assert(has_has_value<poly_int> && has_has_value<result_int>);
    static_assert(has_has_value<poly_void> && has_has_value<result_void>);

    static_assert(has_bool_conversion<poly_int> && has_bool_conversion<result_int>);
    static_assert(has_bool_conversion<poly_void> && has_bool_conversion<result_void>);

    static_assert(has_indirection<poly_int> && has_indirection<result_int>);
    static_assert(has_arrow<poly_int> && has_arrow<result_int>);

    static_assert(has_error_fn<poly_int> && has_error_fn<result_int>);
    static_assert(has_error_fn<poly_void> && has_error_fn<result_void>);
    expect(true);
  };

  // The detectors have to be able to say "absent", or the suite above proves
  // nothing: a concept that is true for everything would pass on a type with no
  // members at all. int has none of these.
  "the detectors report absence rather than hard-erroring"_test = [] {
    static_assert(!has_has_value<int>);
    static_assert(!has_error_fn<int>);
    static_assert(!has_arrow<int>);
    expect(true);
  };

  // Behaviour, not just shape: the subset has to MEAN the same thing on both, or
  // matching signatures would be cosmetic.
  "the subset behaves the same on both backends"_test = [] {
    const poly_int poly_ok{7};
    const result_int result_ok{7};
    expect(poly_ok.has_value() && static_cast<bool>(poly_ok) && *poly_ok == 7);
    expect(result_ok.has_value() && static_cast<bool>(result_ok) && *result_ok == 7);

    const poly_int poly_bad{
        ce::v1::detail::poly::unexpected<ce::error>{ce::error{.code = ce::errc::parse_error}}};
    const result_int result_bad{ce::fail(ce::errc::parse_error)};
    expect(!poly_bad.has_value() && !static_cast<bool>(poly_bad));
    expect(!result_bad.has_value() && !static_cast<bool>(result_bad));
    expect(poly_bad.error().code == ce::errc::parse_error);
    expect(result_bad.error().code == ce::errc::parse_error);
  };

  // expected<void, E> once stored a default-constructed E alongside the flag, so
  // error() on a SUCCESS returned errc{0} - a code with no enumerator, printing
  // as "unknown" - while std::expected leaves that read undefined. A misread
  // behaved differently depending on which backend the build selected, which is
  // the one divergence the polyfill cannot have. The error now lives in a union,
  // which is also why the special members below are hand-written and worth
  // exercising in both states.
  "the void specialisation carries an error only when it holds one"_test = [] {
    const poly_void ok{};
    expect(ok.has_value() && static_cast<bool>(ok));

    const poly_void bad{
        ce::v1::detail::poly::unexpected<ce::error>{ce::error{.code = ce::errc::invalid_base64,
                                                          .detail = "a detail long enough to "
                                                                    "outgrow the small-string "
                                                                    "buffer and allocate",
                                                          .where = "here"}}};
    expect(!bad.has_value());
    expect(bad.error().code == ce::errc::invalid_base64);

    const poly_void copied_bad{bad};
    expect(!copied_bad.has_value() && copied_bad.error().where == "here");
    const poly_void copied_ok{ok};
    expect(copied_ok.has_value());

    poly_void moved_bad{bad};
    const poly_void taken{std::move(moved_bad)};
    expect(!taken.has_value() && taken.error().code == ce::errc::invalid_base64);

    // Assignment across the two alternatives is where a union gets destroyed on
    // the wrong branch, so both directions are covered.
    poly_void assigned{};
    assigned = bad;
    expect(!assigned.has_value() && assigned.error().detail.starts_with("a detail"));
    assigned = ok;
    expect(assigned.has_value());

    poly_void move_assigned{};
    poly_void source{bad};
    move_assigned = std::move(source);
    expect(!move_assigned.has_value() && move_assigned.error().where == "here");
  };
};

// spec: SWR-BUILD-0005
const boost::ut::suite<"config-inline-namespace-v3"> config_inline_namespace_v3 = [] {
  using namespace boost::ut;

  // Compiling AND matching is what pins v3 as inline: if it were a plain
  // namespace `ce::event` would name nothing, and if the names were declared
  // twice is_same_v would be false. That ce::v2::event is a different type is
  // pinned in test/v2/v2_generation_test.cpp, because including a v2 copy here
  // would put the frozen generation under the clang-tidy gate (D-TIDY-5).
  "ce::v3 is inline, so the qualified and unqualified names are one entity"_test = [] {
    static_assert(std::is_same_v<ce::event, ce::v3::event>);
    static_assert(std::is_same_v<ce::data_t, ce::v3::data_t>);
    static_assert(std::is_same_v<ce::timestamp, ce::v3::timestamp>);
    static_assert(std::is_same_v<ce::result<int>, ce::v3::result<int>>);
    expect(true);
  };

  // The attribute types did not change in v0.5.0, so they are declared once, in
  // ce::v2, and brought into ce::v3: code of both generations exchanges them
  // without a conversion.
  "an attribute type shared with v2 is one type through every spelling"_test = [] {
    static_assert(std::is_same_v<ce::v2::id, ce::v3::id>);
    static_assert(std::is_same_v<ce::v2::source, ce::v3::source>);
    static_assert(std::is_same_v<ce::v2::extension_name, ce::v3::extension_name>);
    static_assert(std::is_same_v<ce::v2::attribute_value, ce::v3::attribute_value>);
    static_assert(std::is_same_v<ce::v2::json_text, ce::v3::json_text>);
    static_assert(std::is_same_v<ce::v2::timestamp, ce::v3::timestamp>);
    static_assert(std::is_same_v<ce::v2::message, ce::v3::message>);
    expect(true);
  };

  // An entity unchanged since v0.3.0 is declared once, in ce::v1, and brought into
  // ce::v2 and ce::v3, so every spelling is one type rather than several equal ones.
  "an entity shared with v1 is one type through every spelling"_test = [] {
    static_assert(std::is_same_v<ce::errc, ce::v1::errc>);
    static_assert(std::is_same_v<ce::v2::errc, ce::v3::errc>);
    static_assert(std::is_same_v<ce::v1::errc, ce::v3::errc>);
    static_assert(std::is_same_v<ce::error, ce::v1::error>);
    static_assert(std::is_same_v<ce::failure, ce::v1::failure>);
    static_assert(std::is_same_v<ce::static_error, ce::v1::static_error>);
    static_assert(std::is_same_v<ce::result<int>, ce::v1::result<int>>);
    static_assert(std::is_same_v<ce::v2::result<int>, ce::v3::result<int>>);

    // fail is an overload set, so decltype on the bare name is ambiguous. The
    // address of one overload taken through each spelling says more than a type
    // comparison would: not the same signature, the same function. Compared at run
    // time, because gcc folds the static_assert form into -Wtautological-compare.
    using from_errc = ce::failure (*)(ce::errc, std::string, std::string);
    const auto through_ce = static_cast<from_errc>(&ce::fail);
    const auto through_v1 = static_cast<from_errc>(&ce::v1::fail);
    const auto through_v2 = static_cast<from_errc>(&ce::v2::fail);
    const auto through_v3 = static_cast<from_errc>(&ce::v3::fail);
    expect(through_ce == through_v1);
    expect(through_ce == through_v2);
    expect(through_ce == through_v3);
  };
};

// spec: SWR-BUILD-0006
const boost::ut::suite<"config-v1-immutable"> config_v1_immutable = [] {
  using namespace boost::ut;

  // The v1 event model's survival is pinned in test/v1/v1_generation_test.cpp,
  // outside the clang-tidy gate. What stays here is what both generations share.
  // Renumbering errc is a silent ABI break for anyone who stored one, and errc is
  // shared by both generations.
  "the errc numbering published as v1 is unchanged"_test = [] {
    static_assert(static_cast<int>(ce::v1::errc::missing_required_attribute) == 1);
    static_assert(static_cast<int>(ce::v1::errc::invalid_attribute_name) == 2);
    static_assert(static_cast<int>(ce::v1::errc::invalid_argument) == 15);
    expect(true);
  };
};

}  // namespace

int main() {}
