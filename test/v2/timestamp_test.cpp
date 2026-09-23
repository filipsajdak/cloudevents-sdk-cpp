#include <boost/ut.hpp>

#include <cloudevents/v2/core.hpp>
#include <cloudevents/result.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

// A nanosecond instant expresses nine fractional digits. The count used to be a
// public std::uint8_t, so 10 through 255 were all reachable by hand, and
// rendering one divided a place value that had already reached zero.
//
// The renderer stopped dividing. This is the other half: the count carries its
// own bound, so the state stops existing rather than being survivable.

namespace {

using namespace std::string_view_literals;

}  // namespace

const boost::ut::suite<"timestamp-refuses-more-digits-than-it-can-express">
    timestamp_digit_bound = [] {
  using namespace boost::ut;

  "a count above nine is refused, naming the count"_test = [] {
    for (const std::size_t beyond : {std::size_t{10}, std::size_t{16}, std::size_t{255}}) {
      const auto counted = ce::v2::fraction_digits::make(beyond);
      expect(!counted.has_value()) << "should refuse " << beyond;
      if (!counted) {
        expect(counted.error().code == ce::v2::errc::invalid_attribute_value);
        expect(counted.error().where == std::to_string(beyond));
      }
    }
  };

  "every count a nanosecond instant can express is accepted"_test = [] {
    for (std::size_t count = 0; count <= 9; ++count) {
      const auto counted = ce::v2::fraction_digits::make(count);
      expect(counted.has_value()) << "should accept " << count;
      if (counted) {
        expect(counted->count() == count);
      }
    }
  };

  // The count defaults to none, which is what an instant with no fraction has,
  // and is why `timestamp` stays an aggregate whose only required member is the
  // instant itself.
  "the default is no fractional digits"_test = [] {
    static_assert(ce::v2::fraction_digits{}.count() == 0U);
    static_assert(std::is_aggregate_v<ce::v2::timestamp>);

    const ce::v2::timestamp epoch{
        .utc = std::chrono::sys_time<std::chrono::nanoseconds>{},
    };
    expect(epoch.fractional_digits.count() == 0U);
  };

  // A literal count is checked when the translation unit is compiled, which is
  // what keeps a designated initializer working. An invalid one does not
  // compile; timestamp_digits_probe.cpp is the evidence, since no running
  // program can contain the line.
  "a literal count is checked when compiled"_test = [] {
    constexpr ce::v2::fraction_digits three = 3;
    static_assert(three.count() == 3U);
    static_assert(ce::v2::fraction_digits{9}.count() == 9U);
    expect(true);
  };

  // What the bound protects. Before, this rendered by dividing a place value
  // that was already zero.
  "the parser produces only counts the renderer can express"_test = [] {
    const auto full = ce::v2::parse_timestamp("2018-04-05T17:31:00.123456789Z"sv);
    expect(full.has_value());
    if (full) {
      expect(full->fractional_digits.count() == 9U);
      expect(ce::v2::to_string(*full) == "2018-04-05T17:31:00.123456789Z");
    }

    const auto one = ce::v2::parse_timestamp("2018-04-05T17:31:00.5Z"sv);
    expect(one.has_value());
    if (one) {
      expect(one->fractional_digits.count() == 1U);
    }

    // Ten digits is not RFC 3339 to begin with, so the grammar refuses it before
    // the count is ever built.
    expect(!ce::v2::parse_timestamp("2018-04-05T17:31:00.1234567890Z"sv).has_value());
  };
};

int main() {}
