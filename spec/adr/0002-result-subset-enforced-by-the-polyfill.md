# ADR-0002: The `result<T>` subset is enforced by building against the polyfill

## Status

Accepted 2026-09-19.

## Context

`docs/SPEC.md` §3 rule 3 requires each polyfill to mirror the standard surface
exactly, so that deleting it once the floor rises is a no-op for users. That
requirement cannot be met literally: `std::expected` has a far larger surface than
any reasonable polyfill, including `value()`, `value_or`, `error_or`, `and_then`,
`or_else`, `transform`, `transform_error`, `emplace`, `swap` and `operator==`.

Writing all of that is wasted work, and writing some of it invites call sites that
compile on one configuration and not the other. Worse, `std::expected::value()`
throws `std::bad_expected_access`, which directly violates the no-exceptions rule
from ADR-0001.

A `static_assert`-based surface test cannot solve this. It can assert that the
polyfill *lacks* a member, but it cannot see call sites, so it would not catch the
line that uses `.and_then()` on a configuration where `std::expected` supplies it.

## Decision

Declare a permitted subset and let the compiler enforce it.

The subset is exhaustive: construction from `T`, `std::in_place` or the unexpected
carrier; copy and move; `explicit operator bool()`; `has_value()`; `operator*` and
`operator->` (both with a `has_value()` precondition, never throwing); and
`error()`. `result<void>` carries `operator bool`, `has_value` and `error()` and no
`operator*`. Anything outside that list is banned in `include/`.

Those preconditions are **checked in the polyfill**, which is a deliberate
asymmetry. `std::expected` leaves a wrong-branch read undefined; the polyfill
calls a function that is not `constexpr`, so the same mistake is a compile error
under constant evaluation and an abort at run time. A polyfill may be smaller
than what it stands in for; it may not answer differently. Returning a plausible
wrong value is the one outcome that makes the choice of backend observable, which
is what this ADR exists to prevent. Aborting is consistent with ADR-0001: a
precondition violation is a defect in the caller, not a recoverable failure.
`docs/DECISIONS.md` D-CORE-7 records the divergence this replaced.

Enforcement is three layers, strongest first:

1. **The polyfill is the lint.** CI builds the whole library and the whole test
   suite with `-DCE_FORCE_RESULT_POLYFILL=1` on every compiler in the matrix. Any
   use of a banned member is a hard compile error there. Deleting the polyfill
   later is provably a no-op because the polyfill-only preset was green throughout.
2. **A negative surface test** compiled only in polyfill mode asserts the banned
   members genuinely do not exist, so the polyfill cannot drift into growing them.
3. **A grep lint** over `include/` rejecting the banned spellings, which catches a
   code path guarded by `#if CE_HAS_EXPECTED` that layer 1 would not compile.
   **Not implemented.** Audited 2026-09-21: no such lint exists in
   `.github/workflows/`, in `cmake/`, or in `lefthook.yml`, and the repository has
   no scripts directory. Layers 1 and 2 are real and green; this one was described
   and never built, so the gap it names - a banned spelling inside
   `#if CE_HAS_EXPECTED`, which no configuration compiles - is currently open.

GCC 16 at `-std=c++20` is a load-bearing job for the same reason: it is the natural
configuration where `__cpp_lib_expected` is genuinely absent.

## Consequences

### Positive
- Deleting the polyfill is a mechanical change with compiler-checked evidence
  behind it, which is what §3 rule 3 actually wants.
- `.value()` cannot regress into the codebase, so the `-fno-exceptions` guarantee
  holds by construction rather than by review diligence.

### Negative
- Library code cannot use the monadic operations even where they would read well.
  This is a real ergonomic cost, paid deliberately.
- Every compiler in the matrix builds twice, roughly doubling CI time for the
  library.

### Neutral
- Consumers are not restricted. When `result<T>` is `std::expected`, callers may use
  its full surface; the subset binds this library's own sources only.

## References

- `docs/SPEC.md` §3 rule 3, §5.1
- ADR-0001
