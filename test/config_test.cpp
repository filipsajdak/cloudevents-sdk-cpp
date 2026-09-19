#include <boost/ut.hpp>

#include <cloudevents/detail/config.hpp>

// The configuration header is the single home of every feature gate, so the thing
// worth asserting is not what the flags happen to be on this machine but that they
// are coherent with each other and reachable as constants rather than macros.

namespace {

// spec: SWR-BUILD-0003
const boost::ut::suite<"config-ce-has-constants"> config_ce_has_constants = [] {
  using namespace boost::ut;

  "capabilities are usable in a constant expression"_test = [] {
    static_assert(ce::detail::has_expected == ce::detail::has_expected);
    static_assert(ce::detail::has_reflection == ce::detail::has_reflection);
    static_assert(ce::detail::has_expansion_statements == ce::detail::has_expansion_statements);
    static_assert(ce::detail::has_exceptions == ce::detail::has_exceptions);
    expect(true);
  };

  "capability constants agree with their macros"_test = [] {
    expect(ce::detail::has_expected == (CE_HAS_EXPECTED == 1));
    expect(ce::detail::has_reflection == (CE_HAS_REFLECTION == 1));
    expect(ce::detail::has_expansion_statements == (CE_HAS_EXPANSION_STATEMENTS == 1));
    expect(ce::detail::has_exceptions == (CE_HAS_EXCEPTIONS == 1));
  };
};

// spec: SWR-BUILD-0001
const boost::ut::suite<"config-feature-test-macros-only"> config_feature_test_macros_only = [] {
  using namespace boost::ut;

  // The reflection backend is written in terms of `template for` over a static
  // array, because a splice needs a constant expression. Reflection without
  // expansion statements would therefore be unbuildable; config.hpp turns that
  // into an #error, and this records the invariant for a reader.
  "reflection implies expansion statements"_test = [] {
    expect(!ce::detail::has_reflection || ce::detail::has_expansion_statements);
  };

  // Guarding reflection on __has_include(<meta>) alone would trip here: the
  // header is present at plain -std=c++2c, where reflection is off and expansion
  // statements are on. Measured on GCC 16.2.0; see docs/DECISIONS.md.
  "expansion statements do not imply reflection"_test = [] {
    expect(ce::detail::has_expansion_statements || !ce::detail::has_reflection);
  };
};

// A preset that asks for reflection must actually get it. Without this, dropping
// -freflection from the reflect preset would leave every suite green while the
// C++26 backend silently stopped being compiled at all, which is the failure mode
// a parity suite is least able to notice. The preset defines CE_EXPECT_REFLECTION;
// nothing else does.
#if defined(CE_EXPECT_REFLECTION)
static_assert(ce::detail::has_reflection,
              "this build was configured for the reflection backend, but reflection "
              "is not enabled: -freflection did not reach the compiler");
static_assert(ce::detail::has_expansion_statements,
              "reflection is enabled without expansion statements");
#endif

}  // namespace

int main() {}
