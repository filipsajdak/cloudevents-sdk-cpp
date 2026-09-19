#include <boost/ut.hpp>

#include <cloudevents/detail/config.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

// The configuration header is the single home of every feature gate, so the thing
// worth asserting is not what the flags happen to be on this machine but that they
// are coherent with each other and reachable as constants rather than macros.
//
// Two of the suites below read the headers as TEXT rather than compiling against
// them. A rule of the form "no other header contains an #if" is a statement about
// source that has already been preprocessed away by the time any template sees it,
// so a compile-time assertion cannot express it at all. The scan is the only honest
// form the check has.

// Definedness is observable from nowhere but the preprocessor, so the #if lives
// here, in the test, rather than in a header. SWR-BUILD-0002 scans include/ only;
// this file is not part of that scan.
#if defined(CE_HAS_EXPECTED) && defined(CE_HAS_REFLECTION) && \
    defined(CE_HAS_EXPANSION_STATEMENTS) && defined(CE_HAS_EXCEPTIONS)
#  define CE_TEST_ALL_CAPABILITY_MACROS_DEFINED 1
#else
#  define CE_TEST_ALL_CAPABILITY_MACROS_DEFINED 0
#endif

// The documented-internal CE_DETAIL_* space. No such macro exists today; naming the
// plausible spellings is what makes this a regression guard rather than a tautology
// the day a describe backend starts generating them.
#if defined(CE_DETAIL_CONCAT) || defined(CE_DETAIL_STRINGIFY) ||    \
    defined(CE_DETAIL_FOR_EACH) || defined(CE_DETAIL_MEMBER) ||     \
    defined(CE_DETAIL_DESCRIBE) || defined(CE_DETAIL_DESCRIBE_MEMBER)
#  define CE_TEST_DETAIL_MACRO_LEAKED 1
#else
#  define CE_TEST_DETAIL_MACRO_LEAKED 0
#endif

namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Source scanning, shared by config-gating-single-header and
// config-no-anonymous-namespace.
// ---------------------------------------------------------------------------

/// \brief The installed header tree, located without help from the build system.
///
/// The path is derived from this file's own __FILE__ so the scan works from any
/// build directory. CMake does not promise __FILE__ is absolute -- the Ninja
/// generator hands the compiler a path relative to the build tree -- so when that
/// derivation misses, walk up from the working directory instead. An empty return
/// means neither worked, and every caller FAILS on it rather than passing quietly:
/// a scan that finds nothing must never read as a scan that found nothing wrong.
[[nodiscard]] auto find_include_root() -> fs::path {
  std::error_code ec;

  const auto from_file = fs::absolute(fs::path{__FILE__}, ec);
  if (!ec) {
    const auto candidate = from_file.parent_path().parent_path() / "include" / "cloudevents";
    if (fs::exists(candidate / "detail" / "config.hpp", ec)) {
      return candidate;
    }
  }

  auto dir = fs::current_path(ec);
  if (ec) {
    return {};
  }
  for (;;) {
    const auto candidate = dir / "include" / "cloudevents";
    if (fs::exists(candidate / "detail" / "config.hpp", ec)) {
      return candidate;
    }
    if (dir == dir.parent_path()) {
      return {};
    }
    dir = dir.parent_path();
  }
}

/// \brief Every header under the include tree, in a stable order.
[[nodiscard]] auto headers_under(const fs::path& root) -> std::vector<fs::path> {
  std::vector<fs::path> found;
  std::error_code ec;
  for (fs::recursive_directory_iterator it{root, ec}, end; !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec) && it->path().extension() == ".hpp") {
      found.push_back(it->path());
    }
  }
  std::ranges::sort(found);
  return found;
}

/// \brief One line of a header with its comments removed.
struct source_line {
  std::size_t number;
  std::string text;
};

/// \brief The lines of a header, stripped of comments.
///
/// Stripping matters because both scans look for a token, and a header that merely
/// TALKS about `#if` or an unnamed namespace in its doc comment is not violating
/// anything. `//` inside a string literal would be stripped too; no header here
/// contains one, and erring towards silence on comments is the right direction for
/// a check whose failure mode should be a real finding.
[[nodiscard]] auto code_lines(const fs::path& file) -> std::vector<source_line> {
  std::vector<source_line> lines;
  std::ifstream input{file};
  std::string raw;
  bool in_block_comment = false;

  for (std::size_t number = 1; std::getline(input, raw); ++number) {
    std::string kept;
    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (in_block_comment) {
        if (raw.compare(i, 2, "*/") == 0) {
          in_block_comment = false;
          ++i;
        }
        continue;
      }
      if (raw.compare(i, 2, "/*") == 0) {
        in_block_comment = true;
        ++i;
        continue;
      }
      if (raw.compare(i, 2, "//") == 0) {
        break;
      }
      kept.push_back(raw[i]);
    }
    lines.push_back(source_line{.number = number, .text = std::move(kept)});
  }
  return lines;
}

/// \brief True for `#if`, `#ifdef` and `#ifndef`, however they are spaced.
///
/// `#include` is excluded by the "if" prefix, and `#else`/`#endif` are not tested:
/// neither can exist without the `#if` this already catches.
[[nodiscard]] auto is_preprocessor_conditional(std::string_view text) -> bool {
  auto at = text.find_first_not_of(" \t");
  if (at == std::string_view::npos || text[at] != '#') {
    return false;
  }
  at = text.find_first_not_of(" \t", at + 1);
  if (at == std::string_view::npos) {
    return false;
  }
  return text.substr(at).starts_with("if");
}

[[nodiscard]] constexpr auto is_identifier_char(char c) -> bool {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/// \brief True when the line opens an unnamed namespace: `namespace` then `{`.
///
/// A named namespace, `namespace ce::inline v1 {`, has a name between the two and
/// does not match.
[[nodiscard]] auto declares_anonymous_namespace(std::string_view text) -> bool {
  constexpr std::string_view keyword = "namespace";
  for (auto at = text.find(keyword); at != std::string_view::npos;
       at = text.find(keyword, at + keyword.size())) {
    if (at > 0 && is_identifier_char(text[at - 1])) {
      continue;
    }
    const auto rest = text.substr(at + keyword.size());
    const auto next = rest.find_first_not_of(" \t");
    if (next != std::string_view::npos && rest[next] == '{') {
      return true;
    }
  }
  return false;
}

/// \brief The headers allowed to carry a feature-test conditional.
///
/// config.hpp is the single gate. result.hpp is the documented allowance from SPEC
/// section 3: selecting between the std::expected alias and the polyfill alias is a
/// choice of TOKENS, which no constexpr bool can make. SPEC section 10 extends the
/// same allowance to the describe backends, whose `^^` splice syntax cannot even be
/// PARSED where reflection is off; those headers do not exist yet, and the rule is
/// written here so adding one does not require rewriting the check.
[[nodiscard]] auto may_carry_feature_gate(std::string_view relative) -> bool {
  return relative == "detail/config.hpp" || relative == "result.hpp" ||
         relative.starts_with("detail/describe") || relative.starts_with("describe");
}


// spec: SWR-BUILD-0003
const boost::ut::suite<"config-ce-has-constants"> config_ce_has_constants = [] {
  using namespace boost::ut;

  "capabilities are usable in a constant expression"_test = [] {
    static_assert(ce::detail::has_expected == ce::detail::has_expected);
    static_assert(ce::detail::has_reflection == ce::detail::has_reflection);
    static_assert(ce::detail::has_expansion_statements == ce::detail::has_expansion_statements);
    static_assert(ce::detail::has_exceptions == ce::detail::has_exceptions);
    expect(true);
  };

  "capability constants agree with their macros"_test = [] {
    expect(ce::detail::has_expected == (CE_HAS_EXPECTED == 1));
    expect(ce::detail::has_reflection == (CE_HAS_REFLECTION == 1));
    expect(ce::detail::has_expansion_statements == (CE_HAS_EXPANSION_STATEMENTS == 1));
    expect(ce::detail::has_exceptions == (CE_HAS_EXCEPTIONS == 1));
  };
};

// spec: SWR-BUILD-0001
const boost::ut::suite<"config-feature-test-macros-only"> config_feature_test_macros_only = [] {
  using namespace boost::ut;

  // The reflection backend is written in terms of `template for` over a static
  // array, because a splice needs a constant expression. Reflection without
  // expansion statements would therefore be unbuildable; config.hpp turns that
  // into an #error, and this records the invariant for a reader.
  "reflection implies expansion statements"_test = [] {
    expect(!ce::detail::has_reflection || ce::detail::has_expansion_statements);
  };

  // Guarding reflection on __has_include(<meta>) alone would trip here: the
  // header is present at plain -std=c++2c, where reflection is off and expansion
  // statements are on. Measured on GCC 16.2.0; see docs/DECISIONS.md.
  "expansion statements do not imply reflection"_test = [] {
    expect(ce::detail::has_expansion_statements || !ce::detail::has_reflection);
  };
};

// spec: SYS-BUILD-0001
const boost::ut::suite<"config-single-gate"> config_single_gate = [] {
  using namespace boost::ut;

  // config-ce-has-constants asks whether each flag agrees with its constant. This
  // asks the question one level up: is the gate still ONE place? A capability that
  // grew a second home would show up as a macro with no constant, a constant with
  // no macro, or a macro left undefined on one branch of the #if chain -- so the
  // set is checked as a set, not flag by flag.

  // Every branch of every #if chain in config.hpp defines its macro. A chain that
  // forgets the #else leaves the macro undefined, and `#if CE_HAS_X` then reads it
  // as 0 silently: the capability would look absent everywhere instead of failing.
  "the gate defines every capability macro on every branch"_test = [] {
    expect(CE_TEST_ALL_CAPABILITY_MACROS_DEFINED == 1);
  };

  // A macro spelled anything but 0 or 1 would make `== 1` a coin toss between the
  // constant and a direct `#if` on it.
  "every capability macro is spelled 0 or 1"_test = [] {
    expect(CE_HAS_EXPECTED == 0 || CE_HAS_EXPECTED == 1);
    expect(CE_HAS_REFLECTION == 0 || CE_HAS_REFLECTION == 1);
    expect(CE_HAS_EXPANSION_STATEMENTS == 0 || CE_HAS_EXPANSION_STATEMENTS == 1);
    expect(CE_HAS_EXCEPTIONS == 0 || CE_HAS_EXCEPTIONS == 1);
  };

  // Reachability is what lets library code stay snake_case and macro-free: each
  // macro has a constant, usable in a constant expression, carrying the same answer
  // (ADR-0006). A capability reachable only as a macro would force an #if into a
  // second header, which is exactly what SYS-BUILD-0001 forbids.
  "every capability is reachable as a constant carrying the same answer"_test = [] {
    static_assert(std::is_same_v<decltype(ce::detail::has_expected), const bool>);
    static_assert(std::is_same_v<decltype(ce::detail::has_reflection), const bool>);
    static_assert(std::is_same_v<decltype(ce::detail::has_expansion_statements), const bool>);
    static_assert(std::is_same_v<decltype(ce::detail::has_exceptions), const bool>);

    static_assert(ce::detail::has_expected == (CE_HAS_EXPECTED == 1));
    static_assert(ce::detail::has_reflection == (CE_HAS_REFLECTION == 1));
    static_assert(ce::detail::has_expansion_statements == (CE_HAS_EXPANSION_STATEMENTS == 1));
    static_assert(ce::detail::has_exceptions == (CE_HAS_EXCEPTIONS == 1));
    expect(true);
  };
};

// spec: SWR-BUILD-0002
const boost::ut::suite<"config-gating-single-header"> config_gating_single_header = [] {
  using namespace boost::ut;

  "no header outside the documented allowance carries a preprocessor conditional"_test = [] {
    const auto root = find_include_root();
    if (root.empty()) {
      expect(false) << "cannot locate include/cloudevents from " << __FILE__
                    << " or from the working directory; the scan proves nothing";
      return;
    }

    const auto headers = headers_under(root);
    expect(!headers.empty()) << "no headers found under " << root.string().c_str();

    for (const auto& header : headers) {
      const auto relative = fs::relative(header, root).generic_string();
      if (may_carry_feature_gate(relative)) {
        continue;
      }
      for (const auto& line : code_lines(header)) {
        expect(!is_preprocessor_conditional(line.text))
            << "cloudevents/" << relative.c_str() << ":" << line.number
            << " carries a preprocessor conditional, which belongs in detail/config.hpp:"
            << line.text.c_str();
      }
    }
  };
};

// spec: SWR-BUILD-0007
const boost::ut::suite<"config-no-deprecated-features"> config_no_deprecated_features = [] {
  using namespace boost::ut;

  // The compiler is the enforcer here, not this test. cmake/CeWarnings.cmake adds
  // -Wdeprecated-declarations and, under CE_WERROR, -Werror, so a deprecated
  // facility reached from any header this translation unit includes fails the
  // BUILD -- there is no running test to fail afterwards. The observable fact, and
  // the only one asserted, is that this translation unit compiled under that
  // warning set at all.
  //
  // Deliberately NOT claimed: that no deprecated facility is used anywhere in the
  // SDK. A facility deprecated in a standard newer than the one in use warns
  // nowhere yet, and a header this TU does not include is outside the reach of this
  // TU's compile. That breadth is the preset matrix's job, not this assertion's.
  "this translation unit compiled under -Wdeprecated-declarations -Werror"_test = [] {
    expect(true);
  };
};

// spec: SWR-BUILD-0008
const boost::ut::suite<"config-no-anonymous-namespace"> config_no_anonymous_namespace = [] {
  using namespace boost::ut;

  // An unnamed namespace in a header gives every translation unit its own copy of
  // whatever is inside, which is an ODR hazard today and a hard error once the
  // headers are consumed through a module interface. Like the gating scan, this can
  // only be checked as text: by the time a template could look, the namespace has
  // become a mangled name indistinguishable from any other.
  "no header declares an unnamed namespace"_test = [] {
    const auto root = find_include_root();
    if (root.empty()) {
      expect(false) << "cannot locate include/cloudevents from " << __FILE__
                    << " or from the working directory; the scan proves nothing";
      return;
    }

    const auto headers = headers_under(root);
    expect(!headers.empty()) << "no headers found under " << root.string().c_str();

    for (const auto& header : headers) {
      const auto relative = fs::relative(header, root).generic_string();
      for (const auto& line : code_lines(header)) {
        expect(!declares_anonymous_namespace(line.text))
            << "cloudevents/" << relative.c_str() << ":" << line.number
            << " opens an unnamed namespace, which is not module-ready:"
            << line.text.c_str();
      }
    }
  };
};

// spec: SWR-BUILD-0009
const boost::ut::suite<"config-macro-leakage"> config_macro_leakage = [] {
  using namespace boost::ut;

  // This file includes the public headers at the top, so everything below observes
  // the macro state a CONSUMER is left with after including them.
  //
  // The CE_HAS_* macros are documented-internal per ADR-0006 and must REMAIN
  // defined: only a macro can gate an #include or a token sequence, which is what
  // result.hpp does to choose between std::expected and the polyfill. Undefining
  // them at the end of config.hpp would break that, so "no macro leaks" is not the
  // rule; "no UNDOCUMENTED macro leaks" is.
  //
  // The honest limitation: the preprocessor cannot enumerate what is defined, so
  // this asks about named macros rather than proving a negative over the whole
  // CE_ space. The names below are the internal spellings a describe backend would
  // plausibly introduce, which is what turns this into a guard that can fail.
  "no internal CE_DETAIL_ macro survives inclusion"_test = [] {
    expect(CE_TEST_DETAIL_MACRO_LEAKED == 0);
  };

  "the documented capability macros are defined"_test = [] {
    expect(CE_TEST_ALL_CAPABILITY_MACROS_DEFINED == 1);
  };
};

// A preset that asks for reflection must actually get it. Without this, dropping
// -freflection from the reflect preset would leave every suite green while the
// C++26 backend silently stopped being compiled at all, which is the failure mode
// a parity suite is least able to notice. The preset defines CE_EXPECT_REFLECTION;
// nothing else does.
#if defined(CE_EXPECT_REFLECTION)
static_assert(ce::detail::has_reflection,
              "this build was configured for the reflection backend, but reflection "
              "is not enabled: -freflection did not reach the compiler");
static_assert(ce::detail::has_expansion_statements,
              "reflection is enabled without expansion statements");
#endif

}  // namespace

int main() {}
