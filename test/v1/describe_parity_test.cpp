#include <boost/ut.hpp>

#include <cloudevents/describe.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "describe_parity_checks.hpp"

namespace {

// Macro-described fixtures.
struct point {
  std::int32_t x;
  std::int32_t y;
};

struct payload {
  std::int32_t id;
  double score;
  std::string label;
};

struct containers {
  std::vector<std::string> tags;
  std::optional<std::int64_t> seq;
  std::map<std::string, std::string> meta;
};

struct undescribed {
  std::int32_t value;
};

CE_DESCRIBE(point, x, y);
CE_DESCRIBE(payload, id, CE_FIELD(score, "score_pct"), label);
CE_DESCRIBE(containers, tags, seq, meta);

#if CE_HAS_REFLECTION

// Reflection fixtures carry NO CE_DESCRIBE. Without that, macro precedence would
// make every one of these exercise the macro path twice and report success while
// the reflection backend went uncompiled.
struct [[=ce::v1::reflect{}]] reflected_payload {
  std::int32_t id;
  [[=ce::v1::name("score_pct")]] double score;
  std::string label;
  [[=ce::v1::skip{}]] std::int32_t internal;
};

class [[=ce::v1::reflect{}]] reflected_access {
 public:
  std::int32_t visible;

 private:
  std::int32_t hidden = 0;  // NOLINT(clang-diagnostic-unused-private-field)
};

struct not_opted_in {
  std::int32_t value;
};

// Both descriptions, disagreeing on purpose.
struct [[=ce::v1::reflect{}]] doubly_described {
  std::int32_t id;
  [[=ce::v1::name("refl_name")]] double score;
};

CE_DESCRIBE(doubly_described, id, CE_FIELD(score, "macro_name"));
#endif

const boost::ut::suite<"described-concept"> described_concept = [] {
  using namespace boost::ut;

  "a described type satisfies the concept"_test = [] {
    static_assert(ce::v1::described<point>);
    static_assert(ce::v1::described<payload>);
    expect(true);
  };

  "an undescribed type does not"_test = [] {
    static_assert(!ce::v1::described<undescribed>);
    static_assert(!ce::v1::described<int>);
    expect(true);
  };

  "cv-qualified and reference forms agree with the bare type"_test = [] {
    static_assert(ce::v1::described<const point&>);
    static_assert(ce::v1::described<point&>);
    expect(true);
  };
};

const boost::ut::suite<"for-each-field-order-and-constness"> for_each_field_order = [] {
  using namespace boost::ut;

  "visits in declaration order"_test = [] {
    payload value{.id = 1, .score = 2.0, .label = "l"};
    std::vector<std::string_view> seen;
    ce::v1::for_each_field(value, [&seen](std::string_view n, auto&) { seen.push_back(n); });
    expect(seen == std::vector<std::string_view>{"id", "score_pct", "label"});
  };

  "a non-const object yields writable members"_test = [] {
    point value{.x = 1, .y = 2};
    ce::v1::for_each_field(value, [](std::string_view n, auto& member) {
      if (n == "y") { member = 99; }
    });
    expect(value.y == 99_i);
    expect(value.x == 1_i);
  };

  "a const object yields the same names"_test = [] {
    const payload value{.id = 1, .score = 2.0, .label = "l"};
    std::vector<std::string_view> seen;
    ce::v1::for_each_field(value, [&seen](std::string_view n, const auto&) { seen.push_back(n); });
    expect(seen == std::vector<std::string_view>{"id", "score_pct", "label"});
  };
};

const boost::ut::suite<"field-count-and-names-constexpr"> field_count_and_names = [] {
  using namespace boost::ut;

  "both are usable in a constant expression"_test = [] {
    static_assert(ce::v1::field_count<point> == 2);
    static_assert(ce::v1::field_count<payload> == 3);
    static_assert(ce::v1::field_count<containers> == 3);
    static_assert(ce::v1::field_names<point>().size() == 2);
    static_assert(ce::v1::field_names<point>()[0] == "x");
    expect(true);
  };
};

const boost::ut::suite<"macro-backend-describe"> macro_backend_describe = [] {
  using namespace boost::ut;

  "CE_DESCRIBE names every listed member"_test = [] {
    static_assert(ce::v1::backend_of<point> == ce::v1::describe_backend::macro);
    ce_parity::check<point>({.label = "point", .wire_names = {"x", "y"}});
  };

  "member pointers address the right members"_test = [] {
    point value{.x = 4, .y = 7};
    std::int32_t sum = 0;
    ce::v1::for_each_field(value, [&sum](std::string_view, auto& member) { sum += member; });
    expect(sum == 11_i);
  };
};

const boost::ut::suite<"macro-backend-rename"> macro_backend_rename = [] {
  using namespace boost::ut;

  "CE_FIELD replaces the wire name, leaving the others alone"_test = [] {
    static_assert(ce::v1::field_names<payload>()[0] == "id");
    static_assert(ce::v1::field_names<payload>()[1] == "score_pct");
    static_assert(ce::v1::field_names<payload>()[2] == "label");
    expect(true);
  };
};

const boost::ut::suite<"reflection-backend-describe"> reflection_backend_describe = [] {
  using namespace boost::ut;

#if CE_HAS_REFLECTION
  "reflection describes an opted-in type"_test = [] {
    static_assert(ce::v1::backend_of<reflected_payload> == ce::v1::describe_backend::reflection);
    ce_parity::check<reflected_payload>(
        {.label = "reflected_payload", .wire_names = {"id", "score_pct", "label"}});
  };

  "an annotation renames, and skip excludes"_test = [] {
    static_assert(ce::v1::field_count<reflected_payload> == 3);
    static_assert(ce::v1::field_names<reflected_payload>()[1] == "score_pct");
    expect(true);
  };

  "reflection does not adopt a type that did not opt in"_test = [] {
    static_assert(!ce::v1::described<not_opted_in>);
    expect(true);
  };
#else
  "not exercised without -freflection"_test = [] {
    expect(!ce::v1::detail::has_reflection) << "this preset has no reflection backend to test";
  };
#endif
};

const boost::ut::suite<"reflection-backend-public-only"> reflection_backend_public_only = [] {
  using namespace boost::ut;

#if CE_HAS_REFLECTION
  "private members are excluded"_test = [] {
    static_assert(ce::v1::field_count<reflected_access> == 1);
    static_assert(ce::v1::field_names<reflected_access>()[0] == "visible");
    expect(true);
  };
#else
  "not exercised without -freflection"_test = [] {
    expect(!ce::v1::detail::has_reflection) << "this preset has no reflection backend to test";
  };
#endif
};

const boost::ut::suite<"macro-precedence-under-reflection"> macro_precedence = [] {
  using namespace boost::ut;

#if CE_HAS_REFLECTION
  // The two descriptions disagree on purpose. If this flips, enabling
  // -freflection silently changes a described type's wire format.
  "a macro description wins over reflection"_test = [] {
    static_assert(ce::v1::backend_of<doubly_described> == ce::v1::describe_backend::macro);
    static_assert(ce::v1::field_names<doubly_described>()[1] == "macro_name");
    expect(true);
  };
#else
  "the macro backend is the only one here"_test = [] {
    static_assert(ce::v1::backend_of<payload> == ce::v1::describe_backend::macro);
    expect(true);
  };
#endif
};

const boost::ut::suite<"supported-member-types"> supported_member_types = [] {
  using namespace boost::ut;

  "scalars, strings and the supported containers are accepted"_test = [] {
    static_assert(ce::v1::members_supported<point>());
    static_assert(ce::v1::members_supported<payload>());
    static_assert(ce::v1::members_supported<containers>());
    expect(true);
  };
};

const boost::ut::suite<"unsupported-member-type-static-assert"> unsupported_member_type = [] {
  using namespace boost::ut;

  // A hard static_assert cannot be tested without failing the build, so the
  // predicate behind it is tested instead: it must reject a type the format layer
  // has no mapping for.
  "the predicate rejects an unmappable member type"_test = [] {
    static_assert(ce::v1::detail::supported_field<std::int32_t>);
    static_assert(ce::v1::detail::supported_field<std::string>);
    static_assert(ce::v1::detail::supported_field<std::optional<std::string>>);
    static_assert(!ce::v1::detail::supported_field<void*>);
    static_assert(!ce::v1::detail::supported_field<char>);
    static_assert(!ce::v1::detail::supported_field<std::vector<void*>>);
    expect(true);
  };
};

const boost::ut::suite<"describe-backend-parity"> describe_backend_parity = [] {
  using namespace boost::ut;

  "the macro backend matches its expectation"_test = [] {
    ce_parity::check<payload>({.label = "payload", .wire_names = {"id", "score_pct", "label"}});
    ce_parity::check<containers>({.label = "containers", .wire_names = {"tags", "seq", "meta"}});
  };

#if CE_HAS_REFLECTION
  // The same expectation, against a type described the other way. Identical
  // names, order and visit behaviour is the whole contract of the seam.
  "the reflection backend produces the same description"_test = [] {
    ce_parity::check<reflected_payload>(
        {.label = "reflected_payload", .wire_names = {"id", "score_pct", "label"}});

    const auto macro_names = ce::v1::field_names<payload>();
    const auto reflected_names = ce::v1::field_names<reflected_payload>();
    expect(macro_names.size() == reflected_names.size());
    for (std::size_t i = 0; i < macro_names.size(); ++i) {
      expect(macro_names[i] == reflected_names[i]) << "backends disagree at " << i;
    }
  };
#endif
};

}  // namespace

int main() {}
