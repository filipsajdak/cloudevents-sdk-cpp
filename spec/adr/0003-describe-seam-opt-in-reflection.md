# ADR-0003: One describe seam, with the C++26 backend opt-in

## Status

Accepted 2026-09-19.

## Context

Mapping a user struct to JSON members, to extension attributes and to HTTP headers
is the same problem three times. `docs/SPEC.md` §5.2 solves it once behind a
customization point with two backends: a C++20 `CE_DESCRIBE` macro and a C++26
static reflection backend, with the rule that a type carrying `CE_DESCRIBE` stays
valid under reflection and the macro result takes precedence, "so behaviour cannot
change silently on upgrade".

Precedence alone does not achieve that goal, and this is the substantive finding.
If the reflection backend adopts *any* aggregate, then merely compiling with
`-freflection` makes `described<T>` become **true** for types where it was
previously false. Overload resolution and serialization behaviour would change
across an entire program, for types the author never described. The macro winning
where it is present says nothing about the far larger set of types where it is
absent.

We also measured that GCC 16.2.0 genuinely implements P2996 behind `-freflection`,
so this backend is compiled and tested locally rather than written blind.

## Decision

Keep the single seam (`described<T>`, `for_each_field`, `field_count`,
`field_names`) with declaration order guaranteed on both backends, and make the
reflection backend **opt-in**: it adopts a type only when that type carries
`[[=ce::reflect]]`. `described<T>` therefore has the same truth value on every
compiler.

Three mechanisms together, all compile-time:

1. **Opt-in**, as above, so reflection never silently adopts a type.
2. **Macro wins by construction.** `CE_DESCRIBE` specializations are emitted
   unconditionally -- the macro backend is never compiled out, even under
   `-freflection` -- and dispatch tests the macro descriptor first. The public,
   queryable `ce::backend_of<T>` reports which backend fired, so a test can assert
   it.
3. **Divergence is a build failure.** A `backends_agree<T>()` consteval
   cross-check compares the wire-name sequence the two backends would produce. On
   the reflection preset, a type whose `CE_FIELD` rename disagrees with its
   `[[=ce::name]]` fails to build. Elsewhere the check is vacuously true.

## Consequences

### Positive
- Upgrading the compiler cannot change any type's wire format. That is the
  guarantee §5.2 was reaching for, made actually true.
- Consumers never branch on the backend, which is the seam's whole purpose.
- The reflection backend is verified, not aspirational, because a real toolchain
  compiles it.

### Negative
- Reflection users must annotate their types, so the C++26 backend is not the
  zero-ceremony experience static reflection otherwise offers. This is a deliberate
  trade of convenience for the no-silent-change guarantee.
- This is a refinement of `docs/SPEC.md` §5.2 rather than a literal reading of it,
  and is recorded as such rather than slipped in.

### Neutral
- The parity suite must compile its fixtures twice, with the `CE_DESCRIBE`
  specializations excluded on the reflection translation unit. Otherwise macro
  precedence makes the reflection half of the suite silently test the macro path.

## References

- `docs/SPEC.md` §5.2
- SWR-DESC-0001 through SWR-DESC-0011
- `docs/DECISIONS.md` D-DESC-1, D-DESC-2, D-DESC-3
