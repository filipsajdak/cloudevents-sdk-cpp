#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/result.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <format>
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

// spec: SWR-CORE-0030
const boost::ut::suite<"timestamp-refuses-more-digits-than-it-can-express">
    timestamp_digit_bound = [] {
  using namespace boost::ut;

  "a count above nine is refused, naming the count"_test = [] {
    for (const std::size_t beyond : {std::size_t{10}, std::size_t{16}, std::size_t{255}}) {
      const auto counted = ce::fraction_digits::make(beyond);
      expect(!counted.has_value()) << "should refuse " << beyond;
      if (!counted) {
        expect(counted.error().code == ce::errc::invalid_attribute_value);
        expect(counted.error().where == std::to_string(beyond));
      }
    }
  };

  "every count a nanosecond instant can express is accepted"_test = [] {
    for (std::size_t count = 0; count <= 9; ++count) {
      const auto counted = ce::fraction_digits::make(count);
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
    static_assert(ce::fraction_digits{}.count() == 0U);
    static_assert(std::is_aggregate_v<ce::timestamp>);

    const ce::timestamp epoch{
        .utc = std::chrono::sys_time<std::chrono::nanoseconds>{},
    };
    expect(epoch.fractional_digits.count() == 0U);
  };

  // A literal count is checked when the translation unit is compiled, which is
  // what keeps a designated initializer working. An invalid one does not
  // compile; timestamp_digits_probe.cpp is the evidence, since no running
  // program can contain the line.
  "a literal count is checked when compiled"_test = [] {
    constexpr ce::fraction_digits three = 3;
    static_assert(three.count() == 3U);
    static_assert(ce::fraction_digits{9}.count() == 9U);
    expect(true);
  };

  // What the bound protects. Before, this rendered by dividing a place value
  // that was already zero.
  "the parser produces only counts the renderer can express"_test = [] {
    const auto full = ce::parse_timestamp("2018-04-05T17:31:00.123456789Z"sv);
    expect(full.has_value());
    if (full) {
      expect(full->fractional_digits.count() == 9U);
      expect(ce::to_string(*full) == "2018-04-05T17:31:00.123456789Z");
    }

    const auto one = ce::parse_timestamp("2018-04-05T17:31:00.5Z"sv);
    expect(one.has_value());
    if (one) {
      expect(one->fractional_digits.count() == 1U);
    }

    // Ten digits is not RFC 3339 to begin with, so the grammar refuses it before
    // the count is ever built.
    expect(!ce::parse_timestamp("2018-04-05T17:31:00.1234567890Z"sv).has_value());
  };
};

// RFC 3339 section 5.6 bounds each field of a date-time. A boundary is where an
// off-by-one hides, so every limit gets its last accepted value and its first
// rejected one, and an offset is held to the same limits as the time itself.
namespace {

using namespace std::chrono_literals;

// The limits as the RFC states them. They are spelled here, apart from the
// library's own names for them, so a wrong limit in the library cannot be
// mirrored by the test.
inline constexpr std::chrono::hours last_hour = 23h;
inline constexpr std::chrono::minutes last_minute = 59min;
inline constexpr std::chrono::seconds last_second = 60s;  // a leap second

inline constexpr std::chrono::hours one_hour = 1h;
inline constexpr std::chrono::minutes one_minute = 1min;
inline constexpr std::chrono::seconds one_second = 1s;

struct clock_fields {
  std::chrono::hours hour = {};
  std::chrono::minutes minute = {};
  std::chrono::seconds second = {};
  std::chrono::hours offset_hour = {};
  std::chrono::minutes offset_minute = {};
};

struct limit_case {
  std::string_view name;  // NOLINT(scudoai-copy-view-member,custom-scudoai-copy-view-member)
  clock_fields fields;
  bool accepted;
};

[[nodiscard]] auto render(const clock_fields& fields) -> std::string {
  return std::format("2018-04-05T{:02}:{:02}:{:02}+{:02}:{:02}",
                     fields.hour.count(),
                     fields.minute.count(),
                     fields.second.count(),
                     fields.offset_hour.count(),
                     fields.offset_minute.count());
}

inline constexpr auto limit_cases = std::to_array<limit_case>({
    {.name = "hour at its limit", .fields = {.hour = last_hour}, .accepted = true},
    {.name = "hour past its limit", .fields = {.hour = last_hour + one_hour}, .accepted = false},
    {.name = "minute at its limit", .fields = {.minute = last_minute}, .accepted = true},
    {.name = "minute past its limit",
     .fields = {.minute = last_minute + one_minute},
     .accepted = false},
    {.name = "second at the leap second", .fields = {.second = last_second}, .accepted = true},
    {.name = "second past the leap second",
     .fields = {.second = last_second + one_second},
     .accepted = false},
    {.name = "offset hour at its limit", .fields = {.offset_hour = last_hour}, .accepted = true},
    {.name = "offset hour past its limit",
     .fields = {.offset_hour = last_hour + one_hour},
     .accepted = false},
    {.name = "offset minute at its limit",
     .fields = {.offset_minute = last_minute},
     .accepted = true},
    {.name = "offset minute past its limit",
     .fields = {.offset_minute = last_minute + one_minute},
     .accepted = false},
});

}  // namespace

// spec: SWR-CORE-0008
// spec: SWR-CORE-0010
const boost::ut::suite<"timestamp-field-limits-at-their-boundaries"> timestamp_field_limits = [] {
  using namespace boost::ut;

  "each field accepts its last value and refuses the next"_test = [] {
    for (const auto& limit : limit_cases) {
      const auto text = render(limit.fields);
      const auto parsed = ce::parse_timestamp(text);
      expect(parsed.has_value() == limit.accepted) << limit.name << ": " << text;
      if (!limit.accepted && !parsed) {
        expect(parsed.error().code == ce::errc::invalid_timestamp) << limit.name;
        expect(parsed.error().where == text) << limit.name;
      }
    }
  };

  "the largest offset is kept, not clamped"_test = [] {
    const auto parsed =
        ce::parse_timestamp(render({.offset_hour = last_hour, .offset_minute = last_minute}));
    expect(parsed.has_value());
    if (parsed) {
      expect(parsed->offset == last_hour + last_minute);
    }
  };
};

int main() {}
