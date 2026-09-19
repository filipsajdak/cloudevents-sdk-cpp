#pragma once

/// \file
/// \brief The single home of every feature gate in the SDK.
///
/// SPEC section 3 rule 1: language and library features are detected with
/// feature-test macros only, never with __cplusplus, and every conditional lives
/// here. SPEC section 10 additionally permits #if in the describe backends, which
/// is the one other place it appears, because whether the `^^` splice syntax may
/// be parsed at all cannot be decided by a constexpr bool.
///
/// The CE_HAS_* macros are internal. Library code reads the ce::detail::has_*
/// constants below so that the sources stay snake_case; the macros exist because
/// only a macro can gate an #include or a token sequence. See ADR-0006.

// The sole purpose of this include is to make the __cpp_lib_* library
// feature-test macros visible without dragging in a real standard header.
#include <version>

// ---------------------------------------------------------------------------
// std::expected
// ---------------------------------------------------------------------------
// CE_FORCE_RESULT_POLYFILL builds the polyfill even where std::expected exists.
// ADR-0002 makes that configuration the enforcement job for the result<T> subset:
// any use of a banned member is a hard compile error there, which is what makes
// deleting the polyfill later a provable no-op.
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L && !defined(CE_FORCE_RESULT_POLYFILL)
#  define CE_HAS_EXPECTED 1
#else
#  define CE_HAS_EXPECTED 0
#endif

// ---------------------------------------------------------------------------
// P2996 static reflection
// ---------------------------------------------------------------------------
// Measured on GCC 16.2.0 (see docs/DECISIONS.md). Reflection is gated by a
// compiler FLAG, not by the standard mode, so two tempting guards are wrong and
// both fail confusingly rather than cleanly:
//
//   __has_include(<meta>)        is true at plain -std=c++2c. The header exists
//                                and self-guards on __glibcxx_reflection, so
//                                without -freflection it includes away to
//                                nothing and every use reports "not a member of
//                                std::meta" instead of "header not found".
//   __cpp_expansion_statements   is also true at plain -std=c++2c.
//
// The language macro __cpp_impl_reflection appears only with -freflection, and
// the library macro __cpp_lib_reflection only once <meta> is reachable. Both are
// required.
#ifdef CE_FORCE_DESCRIBE_MACRO
#  define CE_HAS_REFLECTION 0
#elif defined(__cpp_impl_reflection) && defined(__cpp_lib_reflection) && __has_include(<meta>)
#  define CE_HAS_REFLECTION 1
#else
#  define CE_HAS_REFLECTION 0
#endif

#if defined(__cpp_expansion_statements) && __cpp_expansion_statements >= 202506L
#  define CE_HAS_EXPANSION_STATEMENTS 1
#else
#  define CE_HAS_EXPANSION_STATEMENTS 0
#endif

// A splice needs a constant expression, which forces the reflection backend into
// a `template for` shape over a static array. Without expansion statements the
// backend cannot be written at all, so fail loudly here rather than at the first
// confusing use.
#if CE_HAS_REFLECTION && !CE_HAS_EXPANSION_STATEMENTS
#  error "cloudevents: the reflection backend requires expansion statements (template for)"
#endif


// ---------------------------------------------------------------------------
// std::format
// ---------------------------------------------------------------------------
// A hard requirement rather than a capability. The SDK renders timestamps with
// std::format and keeps no second implementation, so a library without it cannot
// build the SDK at all -- and should say so here, in one line, rather than as a
// cascade of errors from <format> not existing.
//
// This is a LIBRARY requirement. GCC 12 has no <format>, and Clang 16 has it with
// libc++ but not with libstdc++ 12, so the compiler version alone does not decide
// it. See SPEC section 8.
#if !defined(__cpp_lib_format) || __cpp_lib_format < 201907L
#  error "cloudevents requires std::format: libstdc++ 13+, libc++ 17+, or MSVC 19.29+. \
Compiler version alone is not enough - GCC 12 has no <format>, and Clang 16 has it \
with libc++ but not with libstdc++ 12."
#endif

#include <format>

// ---------------------------------------------------------------------------
// Exceptions
// ---------------------------------------------------------------------------
// SPEC section 9 decision D4 commits to supporting -fno-exceptions. This constant
// never changes library behaviour; it exists so the test layer can enable
// exception-based checks where the toolchain allows them.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define CE_HAS_EXCEPTIONS 1
#else
#  define CE_HAS_EXCEPTIONS 0
#endif

namespace ce::inline v1::detail {

/// \brief True when std::expected backs ce::result, false when the polyfill does.
inline constexpr bool has_expected = CE_HAS_EXPECTED == 1;

/// \brief True when the C++26 static reflection describe backend is available.
inline constexpr bool has_reflection = CE_HAS_REFLECTION == 1;

/// \brief True when `template for` expansion statements are available.
inline constexpr bool has_expansion_statements = CE_HAS_EXPANSION_STATEMENTS == 1;

/// \brief True when the translation unit is compiled with exceptions enabled.
inline constexpr bool has_exceptions = CE_HAS_EXCEPTIONS == 1;


}  // namespace ce::inline v1::detail
