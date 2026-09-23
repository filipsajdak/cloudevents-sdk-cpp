#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/v1/core.hpp>

#include <cstdint>
#include <type_traits>

namespace {

// spec: SWR-BUILD-0006
const boost::ut::suite<"v1-declarations-survive"> v1_declarations_survive = [] {
  using namespace boost::ut;

  // That a FUTURE breaking change lands in a new generation is a rule about
  // commits that do not exist; review holds anyone to it. What a test can pin is
  // that the breaking change v0.4.0 made left ce::v1 as v0.3.0 published it.
  // Kept apart from build_test.cpp, which the clang-tidy gate lints: including a
  // v1 header there would put the frozen generation under the gate (D-TIDY-4).
  "the v0.4.0 event model did not replace the v1 one"_test = [] {
    static_assert(!std::is_same_v<ce::v1::event, ce::event>);
    static_assert(std::is_aggregate_v<ce::v1::event>);
    static_assert(!std::is_aggregate_v<ce::event>);
    static_assert(std::is_same_v<decltype(ce::v1::timestamp::fractional_digits), std::uint8_t>);
    expect(true);
  };
};

}  // namespace

int main() {}
