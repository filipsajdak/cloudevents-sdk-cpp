# The warning set from CLAUDE.md, applied to everything we compile (tests,
# examples, fuzzers). The library itself is header-only and interface-only, so it
# inherits the consumer's flags; these apply to our own translation units, which
# is where a warning can actually be observed.

add_library(ce_warnings INTERFACE)
add_library(ce::warnings ALIAS ce_warnings)

if(MSVC)
  target_compile_options(ce_warnings INTERFACE
    /W4
    /permissive-
    # /permissive- does NOT imply /Zc:preprocessor. Without the conforming
    # preprocessor, CE_DESCRIBE's argument counting miscounts and generates the
    # wrong field list silently, which is far worse than failing to build.
    # describe_macro.hpp carries an #error on _MSVC_TRADITIONAL as a backstop.
    /Zc:preprocessor
    $<$<BOOL:${CE_WERROR}>:/WX>)
else()
  target_compile_options(ce_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    # Deprecation warnings are errors too: SPEC section 3 rule 5 forbids features
    # removed or deprecated in C++26, and the compiler is the only thing that
    # actually knows which those are.
    -Wdeprecated-declarations
    $<$<BOOL:${CE_WERROR}>:-Werror>)
endif()
