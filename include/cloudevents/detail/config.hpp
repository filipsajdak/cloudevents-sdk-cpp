#pragma once

/// \file
/// \brief Every feature gate in the SDK. Library code reads the `has_*` constants;
/// the `CE_HAS_*` macros exist only where a macro is needed to gate an `#include`
/// or a token sequence. See ADR-0006.

#include <version>

/// Define CE_FORCE_RESULT_POLYFILL to build the polyfill where std::expected
/// exists. This is what enforces the permitted result<T> subset (ADR-0002).
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L && !defined(CE_FORCE_RESULT_POLYFILL)
#  define CE_HAS_EXPECTED 1
#else
#  define CE_HAS_EXPECTED 0
#endif

/// Define CE_FORCE_DESCRIBE_MACRO to select the CE_DESCRIBE backend where
/// reflection is available.
///
/// Both macros are required: `__has_include(<meta>)` and
/// `__cpp_expansion_statements` are each true at plain `-std=c++2c`, where
/// reflection is off.
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

#if CE_HAS_REFLECTION && !CE_HAS_EXPANSION_STATEMENTS
#  error "cloudevents: the reflection backend requires expansion statements (template for)"
#endif

#if !defined(__cpp_lib_format) || __cpp_lib_format < 201907L
#  error "cloudevents requires std::format: libstdc++ 13+, libc++ 17+, or MSVC 19.29+. \
Compiler version alone is not enough - GCC 12 has no <format>, and Clang 16 has it \
with libc++ but not with libstdc++ 12."
#endif

#include <format>

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
