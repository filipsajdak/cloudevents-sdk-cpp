#include <boost/ut.hpp>

#include <cloudevents/result.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

// static_error exists so a rule that decides validity can report WHY at compile
// time. The measurement behind it is in docs/DECISIONS.md and ADR-0008: a
// std::string escapes constant evaluation only when it is short enough for the
// small-string buffer, so an owning error cannot be the compile-time currency.
// These suites check the two halves that matter: it really does survive constant
// evaluation, and widening it into an error loses nothing.

namespace {

using namespace std::string_view_literals;

/// A rule shaped the way every attribute policy is shaped: constexpr, reporting a
/// diagnosis rather than a result, because the compile-time path has no value to
/// carry - its failure mode is a compile error.
[[nodiscard]] constexpr auto check_non_empty(std::string_view text)
    -> std::optional<ce::static_error> {
  if (text.empty()) {
    return ce::static_error{
        .code = ce::errc::missing_required_attribute,
        .detail = "must be non-empty",
        .where = "id",
    };
  }
  return {};
}

}  // namespace

// spec: SWR-CORE-0028
const boost::ut::suite<"static-error-survives-constant-evaluation"> static_error_constexpr = [] {
  using namespace boost::ut;

  // The point of the type. An owning error cannot appear here at all, and a
  // short one appearing while a long one does not would be worse than neither.
  "a diagnosis is usable in a constant expression"_test = [] {
    static constexpr auto absent = check_non_empty(""sv);
    static_assert(absent.has_value());
    static_assert(absent->code == ce::errc::missing_required_attribute);
    static_assert(absent->where == "id"sv);

    static constexpr auto present = check_non_empty("A234"sv);
    static_assert(!present.has_value());
    expect(true);
  };

  // The length dependence is the whole reason this type exists, so the suite
  // carries a detail long enough that an owning string would have to allocate.
  "a long detail is no different from a short one"_test = [] {
    static constexpr ce::static_error verbose{
        .code = ce::errc::invalid_attribute_value,
        .detail = "a detail far longer than any small-string buffer, which is exactly the case "
                  "that decides whether an owning error could have been used here",
        .where = "datacontenttype",
    };
    static_assert(verbose.code == ce::errc::invalid_attribute_value);
    static_assert(verbose.detail.size() > 64U);
    expect(true);
  };

  "it is a literal type and compares by value"_test = [] {
    static_assert(std::is_trivially_copyable_v<ce::static_error>);
    static_assert(ce::static_error{.code = ce::errc::parse_error} ==
                  ce::static_error{.code = ce::errc::parse_error});
    static_assert(!(ce::static_error{.code = ce::errc::parse_error} ==
                    ce::static_error{.code = ce::errc::invalid_utf8}));
    expect(true);
  };
};

// spec: SWR-CORE-0028
const boost::ut::suite<"static-error-widens-into-error"> static_error_widens = [] {
  using namespace boost::ut;

  "widening keeps every part"_test = [] {
    constexpr ce::static_error diagnosis{
        .code = ce::errc::invalid_attribute_name,
        .detail = "extension names must match [a-z0-9]+",
        .where = "extension",
    };
    const ce::error owned = ce::widen(diagnosis);
    expect(owned.code == ce::errc::invalid_attribute_name);
    expect(owned.detail == "extension names must match [a-z0-9]+");
    expect(owned.where == "extension");
  };

  // error stays an aggregate. A converting constructor would have been tidier to
  // call and would have taken designated initialization away from every site
  // that builds one, which is most of them.
  "error is still an aggregate"_test = [] {
    static_assert(std::is_aggregate_v<ce::error>);
    const ce::error built{.code = ce::errc::parse_error, .detail = "d", .where = "w"};
    expect(built.code == ce::errc::parse_error);
  };

  "a diagnosis converts into a failed result of any type"_test = [] {
    constexpr ce::static_error diagnosis{
        .code = ce::errc::out_of_range,
        .detail = "outside the representable range",
    };

    const ce::result<int> number = ce::fail(diagnosis);
    expect(!number.has_value());
    if (!number) {
      expect(number.error().code == ce::errc::out_of_range);
      expect(number.error().detail == "outside the representable range");
    }

    const ce::result<void> nothing = ce::fail(diagnosis);
    expect(!nothing.has_value());
  };

  // The runtime path knows the offending value; the compile-time rule does not.
  "the offending value can be attached on the runtime path"_test = [] {
    constexpr ce::static_error diagnosis{
        .code = ce::errc::invalid_timestamp,
        .detail = "not an RFC 3339 date-time",
        .where = "time",
    };
    const ce::result<int> parsed = ce::fail(diagnosis, std::string{"not-a-date"});
    expect(!parsed.has_value());
    if (!parsed) {
      expect(parsed.error().detail == "not an RFC 3339 date-time");
      expect(parsed.error().where == "not-a-date");
    }
  };
};

int main() {}
