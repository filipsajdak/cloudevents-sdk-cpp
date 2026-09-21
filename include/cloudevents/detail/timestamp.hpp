#pragma once

/// \file
/// \brief RFC 3339 date-time, parsed by one CTRE pattern.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <ctre.hpp>

#include <cloudevents/result.hpp>

namespace ce::inline v1 {

/// \brief How the zero offset was spelled in the source text.
enum class offset_form : std::uint8_t {
  /// `Z` or `z`, the UTC designator.
  utc_designator,
  /// A numeric `+HH:MM` or `-HH:MM` offset.
  numeric,
};

/// \brief An RFC 3339 instant, plus enough of its spelling to reproduce it.
struct timestamp {
  /// The instant, normalised to UTC.
  std::chrono::sys_time<std::chrono::nanoseconds> utc;
  /// The offset the source text carried. Zero for a `Z` timestamp.
  std::chrono::minutes offset = {};
  /// How the offset was written, so `Z` and `+00:00` stay distinguishable.
  offset_form form = offset_form::utc_designator;
  /// Digits after the decimal point in the source, 0 through 9.
  std::uint8_t fractional_digits = 0;

  friend auto operator==(const timestamp&, const timestamp&) -> bool = default;
};

namespace detail {

/// The most fractional digits a nanosecond-resolution instant can express, and
/// the upper bound the RFC 3339 grammar below admits.
inline constexpr std::size_t max_fractional_digits = 9;

inline constexpr int decimal_radix = 10;

/// The one definition of the grammar. The sign class must escape the dash: CTRE
/// rejects a bare `-` inside a character class.
inline constexpr auto rfc3339_pattern = ctll::fixed_string{
    R"(^(\d{4})-(\d{2})-(\d{2})[Tt](\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,9}))?(?:([Zz])|([+\-])(\d{2}):(\d{2}))$)"};

/// \brief Parse a run of decimal digits that CTRE has already shape-checked.
template <class Capture>
[[nodiscard]] constexpr auto to_int(Capture capture) noexcept -> int {
  int value = 0;
  for (const char digit : capture) {
    value = value * 10 + (digit - '0');
  }
  return value;
}

/// \brief True when the year/month/day triple is a real calendar date.
[[nodiscard]] constexpr auto is_valid_date(int year, unsigned month, unsigned day) noexcept -> bool {
  const std::chrono::year_month_day ymd{std::chrono::year{year}, std::chrono::month{month},
                                        std::chrono::day{day}};
  return ymd.ok();
}

}  // namespace detail

/// \brief Parse an RFC 3339 date-time.
///
/// Lenient on consume: lowercase `t`/`z` are accepted, and a seconds field of 60
/// folds onto the following second. Such input does not round-trip; canonical
/// input does. Returns `out_of_range` outside roughly 1678-2262, the span of a
/// nanosecond-resolution clock.
[[nodiscard]] inline auto parse_timestamp(std::string_view text) -> result<timestamp> {
  const auto match = ctre::match<detail::rfc3339_pattern>(text);
  if (!match) {
    return fail(errc::invalid_timestamp, "not an RFC 3339 date-time", std::string{text});
  }

  const int year = detail::to_int(match.get<1>());
  const auto month = static_cast<unsigned>(detail::to_int(match.get<2>()));
  const auto day = static_cast<unsigned>(detail::to_int(match.get<3>()));
  const int hour = detail::to_int(match.get<4>());
  const int minute = detail::to_int(match.get<5>());
  const int second = detail::to_int(match.get<6>());

  if (!detail::is_valid_date(year, month, day)) {
    return fail(errc::invalid_timestamp, "no such calendar date", std::string{text});
  }
  if (hour > 23 || minute > 59) {
    return fail(errc::invalid_timestamp, "hour or minute out of range", std::string{text});
  }
  if (second > 60) {
    return fail(errc::invalid_timestamp, "second out of range", std::string{text});
  }

  std::uint8_t fractional_digits = 0;
  std::chrono::nanoseconds subsecond{0};
  if (const auto fraction = match.get<7>(); fraction) {
    const auto digits = fraction.to_view();
    fractional_digits = static_cast<std::uint8_t>(digits.size());
    auto place = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::milliseconds{100});
    for (const char digit : digits) {
      subsecond += place * (digit - '0');
      place /= 10;
    }
  }

  auto form = offset_form::utc_designator;
  std::chrono::minutes offset{0};
  if (!match.get<8>()) {
    form = offset_form::numeric;
    const int offset_hour = detail::to_int(match.get<10>());
    const int offset_minute = detail::to_int(match.get<11>());
    if (offset_hour > 23 || offset_minute > 59) {
      return fail(errc::invalid_timestamp, "offset out of range", std::string{text});
    }
    offset = std::chrono::hours{offset_hour} + std::chrono::minutes{offset_minute};
    if (match.get<9>().to_view() == "-") {
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

  // Checked in seconds, before widening: the overflow is silent afterwards.
  // One second of headroom so the sub-second part cannot tip it over.
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

/// \brief Render an RFC 3339 date-time, reproducing canonical input exactly.

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
  if (value.fractional_digits > 0) {
    // Rendered as the full nanosecond field and then truncated, rather than by
    // dividing down a place value: the place value reaches zero on the tenth
    // digit, and `fractional_digits` is a public field any caller can set.
    std::array<char, detail::max_fractional_digits> rendered{};
    auto remaining = nanos.count();
    for (auto digit = rendered.rbegin(); digit != rendered.rend(); ++digit) {
      *digit = static_cast<char>('0' + remaining % detail::decimal_radix);
      remaining /= detail::decimal_radix;
    }
    const auto shown = std::min<std::size_t>(value.fractional_digits, rendered.size());
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
                     offset_magnitude / 60, offset_magnitude % 60);
}

}  // namespace ce::inline v1
