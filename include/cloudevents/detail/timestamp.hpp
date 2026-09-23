#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include <ctre.hpp>

#include <cloudevents/result.hpp>

namespace ce::inline v1 {

enum class offset_form : std::uint8_t {
  utc_designator,
  numeric,
};

namespace detail {

inline constexpr std::size_t max_fractional_digits = 9;

inline constexpr int decimal_radix = 10;

inline constexpr int last_hour = 23;
inline constexpr int last_minute = 59;
inline constexpr int leap_second = 60;

inline constexpr int minutes_per_hour = 60;

[[noreturn]] void this_digit_count_is_more_than_a_nanosecond_instant_can_express();

}  // namespace detail

// spec: SWR-CORE-0030
class fraction_digits {
 public:
  constexpr fraction_digits() noexcept = default;

  // NOLINTNEXTLINE(google-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-explicit-constructor)
  consteval fraction_digits(int count) : count_{static_cast<std::uint8_t>(count)} {
    if (count < 0 || std::cmp_greater(count, detail::max_fractional_digits)) {
      detail::this_digit_count_is_more_than_a_nanosecond_instant_can_express();
    }
  }

  [[nodiscard]] static auto make(std::size_t count) -> result<fraction_digits> {
    if (count > detail::max_fractional_digits) {
      return fail(errc::invalid_attribute_value,
                  "a nanosecond instant expresses at most nine fractional digits",
                  std::to_string(count));
    }
    fraction_digits out;
    out.count_ = static_cast<std::uint8_t>(count);
    return out;
  }

  [[nodiscard]] constexpr auto count() const noexcept -> std::uint8_t { return count_; }

  friend auto operator==(fraction_digits, fraction_digits) noexcept -> bool = default;

 private:
  std::uint8_t count_ = 0;
};

// spec: SWR-CORE-0007
struct timestamp {
  std::chrono::sys_time<std::chrono::nanoseconds> utc;
  std::chrono::minutes offset = {};
  offset_form form = offset_form::utc_designator;
  fraction_digits fractional_digits = {};

  friend auto operator==(const timestamp&, const timestamp&) -> bool = default;
};

namespace detail {

inline constexpr auto rfc3339_pattern = ctll::fixed_string{
    R"(^(\d{4})-(\d{2})-(\d{2})[Tt](\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,9}))?(?:([Zz])|([+\-])(\d{2}):(\d{2}))$)"};

namespace rfc3339_group {
inline constexpr std::size_t year = 1;
inline constexpr std::size_t month = 2;
inline constexpr std::size_t day = 3;
inline constexpr std::size_t hour = 4;
inline constexpr std::size_t minute = 5;
inline constexpr std::size_t second = 6;
inline constexpr std::size_t fraction = 7;
inline constexpr std::size_t utc_designator = 8;
inline constexpr std::size_t offset_sign = 9;
inline constexpr std::size_t offset_hour = 10;
inline constexpr std::size_t offset_minute = 11;
}  // namespace rfc3339_group

inline constexpr std::chrono::nanoseconds first_fraction_place =
    std::chrono::nanoseconds{std::chrono::seconds{1}} / decimal_radix;

template <class Capture>
[[nodiscard]] constexpr auto to_int(Capture capture) noexcept -> int {
  int value = 0;
  for (const char digit : capture) {
    value = (value * decimal_radix) + (digit - '0');
  }
  return value;
}

[[nodiscard]] constexpr auto is_valid_date(int year, unsigned month, unsigned day) noexcept -> bool {
  const std::chrono::year_month_day ymd{std::chrono::year{year}, std::chrono::month{month},
                                        std::chrono::day{day}};
  return ymd.ok();
}

}  // namespace detail

// spec: SWR-CORE-0008
// spec: SWR-CORE-0010
// spec: SWR-CORE-0011
[[nodiscard]] inline auto parse_timestamp(std::string_view text) -> result<timestamp> {
  const auto match = ctre::match<detail::rfc3339_pattern>(text);
  if (!match) {
    return fail(errc::invalid_timestamp, "not an RFC 3339 date-time", std::string{text});
  }

  namespace group = detail::rfc3339_group;
  const int year = detail::to_int(match.get<group::year>());
  const auto month = static_cast<unsigned>(detail::to_int(match.get<group::month>()));
  const auto day = static_cast<unsigned>(detail::to_int(match.get<group::day>()));
  const int hour = detail::to_int(match.get<group::hour>());
  const int minute = detail::to_int(match.get<group::minute>());
  const int second = detail::to_int(match.get<group::second>());

  if (!detail::is_valid_date(year, month, day)) {
    return fail(errc::invalid_timestamp, "no such calendar date", std::string{text});
  }
  if (hour > detail::last_hour || minute > detail::last_minute) {
    return fail(errc::invalid_timestamp, "hour or minute out of range", std::string{text});
  }
  if (second > detail::leap_second) {
    return fail(errc::invalid_timestamp, "second out of range", std::string{text});
  }

  fraction_digits fractional_digits{};
  std::chrono::nanoseconds subsecond{0};
  if (const auto fraction = match.get<group::fraction>(); fraction) {
    const auto digits = fraction.to_view();
    auto counted = fraction_digits::make(digits.size());
    if (!counted) {
      return fail(counted.error().code, counted.error().detail, std::string{text});
    }
    fractional_digits = *counted;
    auto place = detail::first_fraction_place;
    for (const char digit : digits) {
      subsecond += place * (digit - '0');
      place /= detail::decimal_radix;
    }
  }

  auto form = offset_form::utc_designator;
  std::chrono::minutes offset{0};
  if (!match.get<group::utc_designator>()) {
    form = offset_form::numeric;
    const int offset_hour = detail::to_int(match.get<group::offset_hour>());
    const int offset_minute = detail::to_int(match.get<group::offset_minute>());
    if (offset_hour > detail::last_hour || offset_minute > detail::last_minute) {
      return fail(errc::invalid_timestamp, "offset out of range", std::string{text});
    }
    offset = std::chrono::hours{offset_hour} + std::chrono::minutes{offset_minute};
    if (match.get<group::offset_sign>().to_view() == "-") {
      offset = -offset;
    }
  }

  const std::chrono::year_month_day ymd{std::chrono::year{year},
                                        std::chrono::month{month},
                                        std::chrono::day{day}};
  const auto days = std::chrono::sys_days{ymd};

  const auto time_of_day =
      std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
  const auto since_epoch =
      std::chrono::duration_cast<std::chrono::seconds>(days.time_since_epoch()) + time_of_day -
      offset;

  constexpr auto representable =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds::max()) -
      std::chrono::seconds{1};
  if (since_epoch > representable || since_epoch < -representable) {
    return fail(errc::out_of_range,
                "instant is outside the range a nanosecond-resolution clock can represent "
                "(roughly the years 1678 to 2262)",
                std::string{text});
  }

  return timestamp{
      .utc = std::chrono::sys_time<std::chrono::nanoseconds>{since_epoch + subsecond},
      .offset = offset,
      .form = form,
      .fractional_digits = fractional_digits,
  };
}

// spec: SWR-CORE-0009
[[nodiscard]] inline auto to_string(const timestamp& value) -> std::string {
  const auto local = value.utc + value.offset;
  const auto days = std::chrono::floor<std::chrono::days>(local);
  const std::chrono::year_month_day ymd{days};
  const auto since_midnight = local - days;

  const auto hours = std::chrono::duration_cast<std::chrono::hours>(since_midnight);
  const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(since_midnight - hours);
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(since_midnight - hours - minutes);
  const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(since_midnight - hours -
                                                                          minutes - seconds);

  std::string fraction;
  if (value.fractional_digits.count() > 0) {
    std::array<char, detail::max_fractional_digits> rendered{};
    auto remaining = nanos.count();
    for (char& digit : rendered | std::views::reverse) {
      digit = static_cast<char>('0' + (remaining % detail::decimal_radix));
      remaining /= detail::decimal_radix;
    }
    const auto shown = std::min<std::size_t>(value.fractional_digits.count(), rendered.size());
    fraction.reserve(shown + 1);
    fraction.push_back('.');
    fraction.append(rendered.data(), shown);
  }

  const auto offset_minutes = value.offset.count();
  const auto offset_magnitude = offset_minutes < 0 ? -offset_minutes : offset_minutes;

  if (value.form == offset_form::utc_designator) {
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}{}Z", static_cast<int>(ymd.year()),
                       static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                       hours.count(), minutes.count(), seconds.count(), fraction);
  }
  return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}{}{}{:02}:{:02}",
                     static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
                     static_cast<unsigned>(ymd.day()), hours.count(), minutes.count(),
                     seconds.count(), fraction, offset_minutes < 0 ? '-' : '+',
                     offset_magnitude / detail::minutes_per_hour,
                     offset_magnitude % detail::minutes_per_hour);
}

}  // namespace ce::inline v1
