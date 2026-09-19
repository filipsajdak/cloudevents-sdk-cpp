# ADR-0006: Every feature gate lives in one configuration header

## Status

Accepted 2026-09-19.

## Context

`docs/SPEC.md` §3 makes cheap language-standard upgrades an explicit goal, and
names the mechanism: feature-test macros only, all in `detail/config.hpp`, each
exposed as a `CE_HAS_*` constant, with no other header testing the compiler or
standard version. Scattered version checks are what turn a standard upgrade into a
port.

Implementation showed the rule cannot be applied literally. `CE_HAS_EXPECTED` must
gate an `#include <expected>`, and `CE_HAS_REFLECTION` must gate both an
`#include <meta>` and whether `^^` splice syntax is parsed at all. No
`constexpr bool` can gate either. `docs/SPEC.md` §10 already anticipates this by
permitting `#if` in the describe backends as well.

## Decision

`detail/config.hpp` performs all detection and all type selection. It includes
`<version>` as its first line, which is what makes `__cpp_lib_*` visible without
pulling in a real header.

`CE_HAS_*` remain live macros, documented as internal. Library code never reads
them; it reads `ce::detail::has_*` `inline constexpr bool` mirrors, so the sources
stay snake_case. `#if` on a `CE_HAS_*` appears only in `detail/config.hpp` and the
describe backends. Every other private macro is `CE_DETAIL_*` and is `#undef`-ed by
the header that defines it.

The reflection guard is `__cpp_impl_reflection` **and** `__cpp_lib_reflection`.
Two plausible alternatives were measured and rejected because they produce false
positives at plain `-std=c++2c`: `__has_include(<meta>)` is true there, and so is
`__cpp_expansion_statements`. A false positive here does not fail cleanly; it
produces a wall of "not a member of std::meta" errors from a header that included
itself away to nothing.

A `#error` fires if reflection is enabled without expansion statements, since the
backend's shape depends on `template for`.

## Consequences

### Positive
- Raising the language floor is a deletion in one file, with the rest of the
  codebase untouched.
- The measured macro table in `docs/DECISIONS.md` means nobody has to re-derive
  which guard is correct, and the two wrong guards are recorded as wrong.

### Negative
- `CE_HAS_*` macros outlive the header, so the "only public macro is CE_DESCRIBE"
  rule is restated rather than met literally. Recorded in `docs/DECISIONS.md`
  D-CONFIG-1 rather than left as an undocumented divergence.

### Neutral
- The C++26 preset sets `CMAKE_CXX_STANDARD 26` and adds only `-freflection`.
  Reflection is gated by a flag rather than by the dialect, so the two are set
  independently.

## References

- `docs/SPEC.md` §3, §10
- SYS-BUILD-0001 and the SWR-BUILD requirements
- `docs/DECISIONS.md` toolchain measurements, D-CONFIG-1, D-CONFIG-2
