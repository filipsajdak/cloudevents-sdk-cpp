#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/detail/config.hpp>
#include <cloudevents/detail/expected_polyfill.hpp>
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

  using poly_int = ce::detail::poly::expected<int, ce::error>;
  using poly_void = ce::detail::poly::expected<void, ce::error>;
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
        ce::detail::poly::unexpected<ce::error>{ce::error{.code = ce::errc::parse_error}}};
    const result_int result_bad{ce::fail(ce::errc::parse_error)};
    expect(!poly_bad.has_value() && !static_cast<bool>(poly_bad));
    expect(!result_bad.has_value() && !static_cast<bool>(result_bad));
    expect(poly_bad.error().code == ce::errc::parse_error);
    expect(result_bad.error().code == ce::errc::parse_error);
  };
};

// spec: SWR-BUILD-0005
const boost::ut::suite<"config-inline-namespace-v1"> config_inline_namespace_v1 = [] {
  using namespace boost::ut;

  // These are not two spellings of a typedef: if v1 were a plain namespace, `ce::X`
  // would name nothing and the file would not compile, and if the names were
  // declared twice they would be different entities and is_same_v would be false.
  // Compiling AND matching is what pins the namespace as inline.
  "ce::v1 is inline, so the qualified and unqualified names are one entity"_test = [] {
    static_assert(std::is_same_v<ce::result<int>, ce::v1::result<int>>);
    static_assert(std::is_same_v<ce::errc, ce::v1::errc>);
    expect(true);
  };

  "the whole published surface lives inside it"_test = [] {
    static_assert(std::is_same_v<ce::error, ce::v1::error>);
    static_assert(std::is_same_v<ce::failure, ce::v1::failure>);
    static_assert(std::is_same_v<ce::event, ce::v1::event>);
    static_assert(std::is_same_v<ce::timestamp, ce::v1::timestamp>);
    static_assert(std::is_same_v<decltype(ce::fail), decltype(ce::v1::fail)>);
    expect(true);
  };
};

// spec: SWR-BUILD-0006
const boost::ut::suite<"config-v1-immutable"> config_v1_immutable = [] {
  using namespace boost::ut;

  // Only part of this requirement is observable from inside the program. That a
  // FUTURE breaking change lands in ce::v2 instead of mutating ce::v1 is a rule
  // about commits that do not exist; no assertion can hold anyone to it, and review
  // is what does. Asserting it anyway would be the dishonest move, so this suite
  // does not.
  //
  // What a test CAN pin is the mechanism the rule depends on: v1-qualified
  // spellings resolve today and name the same entities as the unqualified ones. A
  // consumer that writes ce::v1::result therefore already has the pin it would need
  // if a v2 appeared, and adding v2 alongside v1 cannot change what those spellings
  // mean. If someone ever moved a declaration OUT of v1, these stop compiling.
  "v1-qualified spellings resolve to the published entities"_test = [] {
    static_assert(std::is_same_v<ce::v1::errc, ce::errc>);
    static_assert(std::is_same_v<ce::v1::error, ce::error>);
    static_assert(std::is_same_v<ce::v1::result<int>, ce::result<int>>);
    static_assert(std::is_same_v<ce::v1::event, ce::event>);
    expect(true);
  };

  // The enumerator values are part of the published API in a way the type identity
  // above does not cover: renumbering errc is a silent ABI break for anyone who
  // stored one. Pinning them is the part of "v1 keeps its existing declarations"
  // that a test can actually enforce.
  "the errc numbering published as v1 is unchanged"_test = [] {
    static_assert(static_cast<int>(ce::v1::errc::missing_required_attribute) == 1);
    static_assert(static_cast<int>(ce::v1::errc::invalid_attribute_name) == 2);
    static_assert(static_cast<int>(ce::v1::errc::invalid_argument) == 15);
    expect(true);
  };
};

}  // namespace

int main() {}
