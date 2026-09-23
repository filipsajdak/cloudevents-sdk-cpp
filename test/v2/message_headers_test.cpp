#include <boost/ut.hpp>

#include <cloudevents/v2/binding/common.hpp>
#include <cloudevents/v2/core.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <string>
#include <string_view>

// Two types, because a transport hands the SDK whatever arrived and the SDK
// hands a transport only what it meant. raw_headers carries the first; headers
// is the second, and adopt() is the gate between them.
//
// The rule these suites pin was a real divergence, not a hypothetical: find()
// returned the FIRST field of a name while read_attributes let the LAST one win,
// so a message carrying ce-id twice decoded differently from how its content
// mode was detected. Nothing in the suite caught it, because nothing sent one.

namespace {

using namespace std::string_view_literals;

namespace binding = ce::v2::binding;

/// Byte-exact names, values unescaped: the shape every non-HTTP binding has.
struct exact_traits {
  static constexpr std::string_view attribute_prefix = "x_";
  static constexpr std::string_view content_type_header = "content-type";
  static constexpr bool case_sensitive_names = true;

  [[nodiscard]] static auto encode_value(std::string_view text) -> ce::v2::result<std::string> {
    return std::string{text};
  }
  [[nodiscard]] static auto decode_value(std::string_view text) -> ce::v2::result<std::string> {
    return std::string{text};
  }
};

/// Case-insensitive names, as HTTP has them.
struct lenient_traits : exact_traits {
  static constexpr bool case_sensitive_names = false;
};

/// A template on purpose: a requires-expression naming a member of a CONCRETE
/// type hard-errors rather than evaluating to false, which is the same trap the
/// codec suite documents for its nlohmann-shape detectors.
template <class H>
concept has_add = requires(H fields) { fields.add("a", "b"); };

}  // namespace

const boost::ut::suite<"headers-adopt-refuses-what-it-cannot-hold"> headers_adopt = [] {
  using namespace boost::ut;

  "fields that arrived once each are adopted unchanged"_test = [] {
    const ce::v2::raw_headers delivered{
        {"x_specversion", "1.0"},
        {"x_id", "1"},
        {"x_source", "/s"},
    };

    const auto adopted = ce::v2::headers::adopt(delivered, ce::v2::name_matching::case_sensitive);
    expect(adopted.has_value());
    if (!adopted) {
      return;
    }
    expect(adopted->size() == 3U);
    expect(adopted->to_raw() == delivered) << "adoption must not reorder or rewrite";
  };

  "an empty set adopts to an empty set"_test = [] {
    const auto adopted = ce::v2::headers::adopt(ce::v2::raw_headers{}, ce::v2::name_matching::case_sensitive);
    expect(adopted.has_value());
    if (adopted) {
      expect(adopted->empty());
    }
  };

  // The invariant exists so nothing downstream has to choose. A type that let a
  // second field of one name in would put the choice back.
  "the invariant type offers no way to add a second field of a name"_test = [] {
    static_assert(!has_add<ce::v2::headers>,
                  "headers must not offer add: a repeated name is what it excludes");
    // The detector has to be able to say "present", or the assertion above would
    // pass against a concept that is false for everything.
    static_assert(has_add<ce::v2::raw_headers>);
    expect(true);
  };

  "the permissive type still keeps everything that arrived"_test = [] {
    const ce::v2::raw_headers delivered{
        {"x_id", "first"},
        {"x_id", "second"},
    };
    expect(delivered.size() == 2U) << "raw_headers is what a transport delivered, not a judgement";
  };
};

const boost::ut::suite<"headers-adopt-refuses-a-repeated-attribute"> headers_repeated = [] {
  using namespace boost::ut;

  "a repeated field name is refused, naming the field"_test = [] {
    const ce::v2::raw_headers delivered{
        {"x_specversion", "1.0"},
        {"x_id", "first"},
        {"x_source", "/s"},
        {"x_id", "second"},
    };

    const auto adopted = ce::v2::headers::adopt(delivered, ce::v2::name_matching::case_sensitive);
    expect(!adopted.has_value());
    if (!adopted) {
      expect(adopted.error().code == ce::v2::errc::invalid_argument);
      expect(adopted.error().where == "x_id"sv);
    }
  };

  // Whether two spellings are one attribute is the binding's rule, not this
  // type's assumption: HTTP says they are, Kafka says they are not.
  "case folding follows the binding's rule"_test = [] {
    const ce::v2::raw_headers mixed{
        {"x_id", "first"},
        {"X_ID", "second"},
    };

    const auto strict = ce::v2::headers::adopt(mixed, ce::v2::name_matching::case_sensitive);
    expect(strict.has_value()) << "byte-exact names make these two different fields";

    const auto lenient = ce::v2::headers::adopt(mixed, ce::v2::name_matching::case_insensitive);
    expect(!lenient.has_value()) << "case-insensitive names make these one attribute twice";
    if (!lenient) {
      expect(lenient.error().code == ce::v2::errc::invalid_argument);
    }
  };

  // The divergence this closes. Before, find() answered "first" and
  // read_attributes answered "second", and both were reachable from one message.
  "a decoder refuses the message rather than choosing a value"_test = [] {
    const ce::v2::raw_headers delivered{
        {"x_specversion", "1.0"},
        {"x_id", "first"},
        {"x_source", "/s"},
        {"x_type", "t"},
        {"x_id", "second"},
    };

    const auto decoded = binding::read_attributes<exact_traits>(delivered);
    expect(!decoded.has_value());
    if (!decoded) {
      expect(decoded.error().code == ce::v2::errc::invalid_argument);
      expect(decoded.error().where == "x_id"sv);
    }
  };

  "a case-insensitive binding refuses what a case-sensitive one accepts"_test = [] {
    const ce::v2::raw_headers delivered{
        {"x_specversion", "1.0"},
        {"x_id", "1"},
        {"x_source", "/s"},
        {"x_type", "t"},
        {"X_ID", "also"},
    };

    expect(binding::read_attributes<exact_traits>(delivered).has_value())
        << "byte-exact: X_ID carries no prefix this binding recognises as x_";
    expect(!binding::read_attributes<lenient_traits>(delivered).has_value())
        << "case-insensitive: X_ID is ce-id arriving a second time";
  };
};

int main() {}
