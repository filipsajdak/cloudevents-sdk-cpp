#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

#include <boost/ut.hpp>

// The build preprocesses test/macro_probe/headers.cpp and baseline.cpp with
// -dM -E, using this preset's compiler and flags, and hands the two dumps here.
// A macro the headers leave defined is one the first dump has and the second,
// which includes nothing, does not.

namespace {

using name_set = std::set<std::string, std::less<>>;

constexpr std::string_view define_prefix = "#define ";
constexpr std::string_view sdk_prefix = "CE_";

/// The CE_-prefixed macro names defined in a -dM dump; an unreadable file gives
/// an empty set, which the suite reports rather than reading as "nothing leaked".
[[nodiscard]] auto sdk_macros_in(const char* path) -> name_set {
  name_set names;
  std::ifstream dump{path};
  for (std::string line; std::getline(dump, line);) {
    std::string_view rest{line};
    if (!rest.starts_with(define_prefix)) {
      continue;
    }
    rest.remove_prefix(define_prefix.size());
    const auto name = rest.substr(0, rest.find_first_of(" ("));
    if (name.starts_with(sdk_prefix)) {
      names.emplace(name);
    }
  }
  return names;
}

[[nodiscard]] auto minus(const name_set& from, const name_set& removed) -> name_set {
  name_set difference;
  std::ranges::set_difference(from, removed, std::inserter(difference, difference.end()));
  return difference;
}

[[nodiscard]] auto joined(const name_set& names) -> std::string {
  std::string text;
  for (const auto& name : names) {
    text += text.empty() ? "" : ", ";
    text += name;
  }
  return text;
}

// The public macro.
constexpr std::string_view public_macro = "CE_DESCRIBE";

// The reserved names SWR-BUILD-0009 and D-CONFIG-1 document by name.
[[nodiscard]] auto reserved_by_name() -> const name_set& {
  static const name_set names{
      "CE_FIELD",
      "CE_HAS_EXCEPTIONS",
      "CE_HAS_EXPANSION_STATEMENTS",
      "CE_HAS_EXPECTED",
      "CE_HAS_REFLECTION",
  };
  return names;
}

// The reserved family SWR-BUILD-0009 documents by prefix.
constexpr std::string_view reserved_prefix = "CE_DETAIL_";

// Every CE_DETAIL_ helper the headers define today. The requirement documents
// the prefix, not these names, so this list is what catches a helper being added
// or removed without anyone looking at the macro surface.
[[nodiscard]] auto detail_helpers() -> const name_set& {
  static const name_set names{
      "CE_DETAIL_ARGN",      "CE_DETAIL_CAT",      "CE_DETAIL_CAT_IMPL",      "CE_DETAIL_ENTRY",
      "CE_DETAIL_ENTRY_0",   "CE_DETAIL_ENTRY_1",  "CE_DETAIL_ENTRY_1A",      "CE_DETAIL_ENTRY_1B",
      "CE_DETAIL_FE",        "CE_DETAIL_FE_1",     "CE_DETAIL_FE_2",          "CE_DETAIL_FE_3",
      "CE_DETAIL_FE_4",      "CE_DETAIL_FE_5",     "CE_DETAIL_FE_6",          "CE_DETAIL_FE_7",
      "CE_DETAIL_FE_8",      "CE_DETAIL_FE_9",     "CE_DETAIL_FE_10",         "CE_DETAIL_FE_11",
      "CE_DETAIL_FE_12",     "CE_DETAIL_FE_13",    "CE_DETAIL_FE_14",         "CE_DETAIL_FE_15",
      "CE_DETAIL_FE_16",     "CE_DETAIL_FE_17",    "CE_DETAIL_FE_18",         "CE_DETAIL_FE_19",
      "CE_DETAIL_FE_20",     "CE_DETAIL_FE_21",    "CE_DETAIL_FE_22",         "CE_DETAIL_FE_23",
      "CE_DETAIL_FE_24",     "CE_DETAIL_FE_25",    "CE_DETAIL_FE_26",         "CE_DETAIL_FE_27",
      "CE_DETAIL_FE_28",     "CE_DETAIL_FE_29",    "CE_DETAIL_FE_30",         "CE_DETAIL_FE_31",
      "CE_DETAIL_FE_32",     "CE_DETAIL_IS_PAREN", "CE_DETAIL_IS_PAREN_IMPL", "CE_DETAIL_NARG",
      "CE_DETAIL_NARG_IMPL", "CE_DETAIL_PROBE",    "CE_DETAIL_SECOND",        "CE_DETAIL_UNPAREN",
  };
  return names;
}

[[nodiscard]] auto documented_set() -> name_set {
  name_set names{reserved_by_name()};
  names.emplace(public_macro);
  names.insert(detail_helpers().begin(), detail_helpers().end());
  return names;
}

// spec: SWR-BUILD-0009
const boost::ut::suite<"macro-reserved-set"> macro_reserved_set = [] {
  using namespace boost::ut;

  const auto from_headers = sdk_macros_in(MACRO_PROBE_HEADERS_DUMP);
  const auto from_command_line = sdk_macros_in(MACRO_PROBE_BASELINE_DUMP);
  const auto left_defined = minus(from_headers, from_command_line);

  "the probe dump was read"_test = [&] {
    expect(from_headers.contains(public_macro) >> fatal)
        << "no CE_DESCRIBE in" << MACRO_PROBE_HEADERS_DUMP;
  };

  "every macro the headers leave defined is public or reserved"_test = [&] {
    name_set undocumented;
    for (const auto& name : left_defined) {
      const bool reserved = name == public_macro || reserved_by_name().contains(name) ||
                            name.starts_with(reserved_prefix);
      if (!reserved) {
        undocumented.insert(name);
      }
    }
    expect(undocumented.empty()) << "undocumented macros left defined:" << joined(undocumented);
  };

  "the headers define exactly the pinned set"_test = [&] {
    const auto expected = documented_set();
    const auto added = minus(left_defined, expected);
    const auto gone = minus(expected, left_defined);
    expect(added.empty()) << "defined but not pinned:" << joined(added);
    expect(gone.empty()) << "pinned or documented but no longer defined:" << joined(gone);
  };
};

}  // namespace

int main() {}
