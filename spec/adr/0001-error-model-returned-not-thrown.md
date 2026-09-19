# ADR-0001: Recoverable failures are returned, never thrown

## Status

Accepted 2026-09-19.

## Context

`docs/SPEC.md` §9 decision D4 commits the SDK to supporting `-fno-exceptions`
builds, and `CLAUDE.md` forbids throwing from library code. The first consumers
are C++20 services that compile with exceptions disabled, so an error model built
on exceptions would put the library out of reach regardless of its correctness.

Separately, the SDK spans a C++20 floor where `std::expected` does not exist and a
C++23-and-later world where it does. GCC 16 at `-std=c++20` does not define
`__cpp_lib_expected`, which we measured rather than assumed.

## Decision

Every fallible operation returns `ce::result<T>`, which aliases
`std::expected<T, ce::error>` when `__cpp_lib_expected` is available and a
hand-written polyfill otherwise. `ce::error` is an aggregate carrying an `errc`
code, a human-readable detail string and an RFC 6901 JSON pointer that is empty
when not applicable.

`ce::fail(code, detail, pointer)` returns the *unexpected carrier* rather than a
`result<T>`, so a single helper converts into any `result<U>` return type on both
backends without deduction.

## Consequences

### Positive
- The library is usable from a translation unit compiled with `-fno-exceptions`.
- Error information is structured. A JSON pointer makes a decode failure locatable
  in the offending document rather than merely named.
- The C++20 floor is a real, tested configuration rather than a claim.

### Negative
- Error propagation is verbose without a `TRY`-style helper, and verbosity invites
  a `.value()` slip. An internal-only `CE_DETAIL_TRY` is provided, used solely
  inside `include/cloudevents/`, and never documented as public API.
- Two `result<T>` implementations exist until the floor rises to C++23.

### Neutral
- Mixing translation units built at different `-std` levels within one program
  yields two distinct `ce::result<T>` types. This is documented as a constraint and
  checked by a CI job that links one of each and expects a loud failure.

## References

- `docs/SPEC.md` §5.1, §9 decision D4
- SWR-ADOPT-0003, SWR-CORE requirements covering `result<T>` and `errc`
- ADR-0002 (the enforced subset that makes the polyfill removable)
