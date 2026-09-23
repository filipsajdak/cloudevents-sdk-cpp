#pragma once

// spec: SYS-BUILD-0001
// spec: SWR-BUILD-0002

#include <version>

// spec: SWR-BUILD-0001
// spec: SWR-CORE-0002
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L && !defined(CE_FORCE_RESULT_POLYFILL)
#  define CE_HAS_EXPECTED 1
#else
#  define CE_HAS_EXPECTED 0
#endif

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
#  if !(defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 170000)
#    error "cloudevents requires std::format: libstdc++ 13+, libc++ 17+, or MSVC 19.29+. \
Compiler version alone is not enough - GCC 12 has no <format>, and libc++ 16 has the \
header without std::format in it."
#  endif
#endif

#include <format>

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define CE_HAS_EXCEPTIONS 1
#else
#  define CE_HAS_EXCEPTIONS 0
#endif

namespace ce::v1::detail {

// spec: SWR-BUILD-0003
inline constexpr bool has_expected = CE_HAS_EXPECTED == 1;

inline constexpr bool has_reflection = CE_HAS_REFLECTION == 1;

inline constexpr bool has_expansion_statements = CE_HAS_EXPANSION_STATEMENTS == 1;

inline constexpr bool has_exceptions = CE_HAS_EXCEPTIONS == 1;

}  // namespace ce::v1::detail
