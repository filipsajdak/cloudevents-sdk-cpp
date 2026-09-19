#pragma once

/// \file
/// \brief RFC 3339 date-time, parsed by one CTRE pattern.
///
/// SPEC §5.1 fixes three things about this type that are easy to get subtly wrong:
/// parsing goes through a single CTRE pattern (never `std::chrono::parse`, and
/// never `std::regex`), `to_string` reproduces canonical input byte for byte, and
/// the parser is lenient where the spec says a receiver should be.
///
/// Byte-for-byte round-tripping is why this type stores more than an instant. Two
/// timestamps denoting the same moment can be spelled differently and both be
/// canonical: `2018-04-05T17:31:00Z` and `2018-04-05T17:31:00+00:00` are the same
/// instant, and `...T17:31:00.000Z` differs from `...T17:31:00Z` only in
/// precision the instant does not record. Discarding the spelling would make an
/// event that arrives and leaves again not equal to itself.

#include <chrono>
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
///
/// Required member first with no default initializer, so a designated initializer
/// that omits the instant fails to compile rather than silently meaning the epoch.
struct timestamp {
  /// The instant, normalised to UTC.
  std::chrono::sys_time<std::chrono::nanoseconds> utc;
  /// The offset the source text carried. Zero for a `Z` timestamp.
  std::chrono::minutes offset = {};
  /// How the offset was written, so `Z` and `+00:00` stay distinguishable.
  offset_form form = offset_form::utc_designator;
  /// Digits after the decimal point in the source, 0 through 9. Preserved so
  /// that trailing zeros, which carry no information about the instant, survive.
  std::uint8_t fractional_digits = 0;

  friend auto operator==(const timestamp&, const timestamp&) -> bool = default;
};

namespace detail {

/// The one pattern. Case-insensitive `T`/`Z` and a seconds field reaching 60 are
/// accepted here rather than in a second pass, so there is exactly one definition
/// of the grammar (SWR-CORE-0008, SWR-CORE-0010).
///
/// The sign class escapes the dash (`[+\-]`). CTRE rejects a bare `-` inside a
/// character class at either end, not just as a trailing range; both `[+-]` and
/// `[-+]` fail to compile. The diagnostic is only a character offset into the
/// pattern, so the answer is recorded here rather than rediscovered.
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
/// Lenient on consume, per SPEC §5.1: a lowercase `t` or `z` is accepted, a space
/// is accepted in place of `T`, and a seconds field of 60 is accepted so a leap
/// second does not become a decode failure. A leap second is folded onto the
/// following second, because `sys_time` has no representation for it; such an
/// input therefore does not round-trip, which canonical input does.
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
  // 60 is permitted: RFC 3339 §5.6 allows it for a leap second, and a receiver
  // rejecting it would fail on legitimate traffic twice a year or so.
  if (second > 60) {
    return fail(errc::invalid_timestamp, "second out of range", std::string{text});
  }

  std::uint8_t fractional_digits = 0;
  std::chrono::nanoseconds subsecond{0};
  if (const auto fraction = match.get<7>(); fraction) {
    const auto digits = fraction.to_view();
    fractional_digits = static_cast<std::uint8_t>(digits.size());
    // Each digit contributes its own place value, carried as a duration so the
    // units stay in the type: ".5" is 500ms because the first place IS 100ms,
    // not because a bare integer was scaled by the right power of ten.
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

  // Every quantity below is a chrono duration rather than a bare integer, so the
  // unit is part of the type and a minutes-for-seconds mix-up is a compile error
  // instead of an instant that is wrong by a factor of sixty.
  const auto time_of_day =
      std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
  const auto since_epoch =
      std::chrono::duration_cast<std::chrono::seconds>(days.time_since_epoch()) + time_of_day -
      offset;

  // The range check happens in seconds, BEFORE widening to nanoseconds. Checking
  // afterwards cannot work: sys_time<nanoseconds> counts nanoseconds in an int64
  // and so spans only about 1678 to 2262, while RFC 3339 admits any four-digit
  // year, and the overflow is silent. Measured before this check existed,
  // 9999-12-31 parsed cleanly and came back as 1816-03-30. This is a decode path
  // any peer can reach, so it returns a typed error (docs/DECISIONS.md D-CORE-2).
  //
  // One second of headroom, so adding the sub-second part cannot tip it over.
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

  // The fraction and the offset are built first, because both are optional and
  // their presence depends on how the source text was written rather than on the
  // instant. Doing them separately keeps one rendering expression below.
  std::string fraction;
  if (value.fractional_digits > 0) {
    fraction.reserve(std::size_t{value.fractional_digits} + 1);
    fraction.push_back('.');
    // Divide duration by duration to get the digit at each place, so the place
    // value carries its unit instead of being a power of ten that has to agree
    // with nanosecond resolution by convention.
    auto place =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{100});
    for (std::uint8_t i = 0; i < value.fractional_digits; ++i) {
      fraction.push_back(static_cast<char>('0' + (nanos / place) % 10));
      place /= 10;
    }
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
