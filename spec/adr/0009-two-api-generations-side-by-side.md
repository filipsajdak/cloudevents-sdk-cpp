# ADR-0009: Two API generations side by side

## Status

Accepted 2026-09-23, on CR-0002. Supersedes nothing; it implements `docs/SPEC.md` section 3
rule 4 for the first time.

## Context

v0.4.0 changes most of what v0.3.0 published (CR-0002 lists it). Rule 4 says those changes
belong in `ce::v2` and that `ce::v1` keeps its declarations. Three constraints decide how.

**`CE_DESCRIBE` can have only one definition.** A translation unit that includes both
generations sees the macro twice, and a second definition with different tokens is
ill-formed. So the describe seam the macro expands into is shared, and it has to be the
v1 seam, because v1's declarations may not change.

**An inline namespace hides the non-inline one.** Once `v2` is inline, `ce::errc` finds
nothing in `ce::v1`. A shared entity is therefore brought into `v2` explicitly, and then
`ce::errc`, `ce::v1::errc` and `ce::v2::errc` name one type.

**A shared entity must not reach an event.** Anything whose declaration mentions `event`,
`timestamp`, `attribute_value` or `headers` differs between the generations and cannot be
shared.

## Decision

**Layout.** `include/cloudevents/` holds v2 and the shared headers.
`include/cloudevents/v1/` holds the v1 copies of the changed headers, restored from the
v0.3.0 tag, in `namespace ce::v1` and including each other by their `v1/` paths.

**Shared, declared once in `ce::v1`:**

| header | entities |
|---|---|
| `result.hpp` | `errc`, `to_string_view`, `error`, `static_error`, `widen`, `result`, `failure`, `fail` |
| `detail/config.hpp`, `detail/expected_polyfill.hpp` | the feature gates and the polyfill |
| `format/json_codec.hpp` | `json::kind`, `json::json_codec`, the two media types |
| `codec/*.hpp` | the three codecs |
| `describe.hpp`, `detail/describe_macro.hpp`, `detail/describe_reflection.hpp` | the describe seam, `CE_DESCRIBE`, `CE_FIELD` |

Each shared header ends by bringing its entities into `ce::inline v2`: a using-declaration
per entity, and a namespace alias for `json` and `codec`, which only shared headers open.

**The describe seam returns to its v0.3.0 declarations.** `ce::name::value` is a `char[N]`
again and `for_each_field` takes `F&&`. The clang-tidy findings those two shapes raise are
suppressed at the site, with the reason in `D-TIDY-3`.

**Restored in `ce::v1`:** `core.hpp`, `detail/timestamp.hpp`, `message.hpp`,
`extensions.hpp`, `format/base64.hpp`, `format/describe_json.hpp`, `format/json_format.hpp`,
`format/typed_payload.hpp`, and the binding headers. Each gets the fixes that keep its
declarations, and nothing else.

**Tests.** The v0.3.0 suites, examples and fuzzers are restored under `test/v1/`,
`examples/v1/` and `fuzz/v1/`, spelled against `ce::v1`. Their `// spec:` markers are
removed, because the requirements now describe v2. `SWR-BUILD-0011` names them as a whole,
and a v1 defect fix gets its own suite, marked for the requirement it satisfies.

**The module exports v2 only.** Re-exporting `ce::v1` from `cloudevents.cppm` would need a
using-declaration of each v1 name into `ce::v1` itself, which redeclares it in its own
scope and exports nothing (`D-MODULE-1`). The module is off by default; a v1 consumer
includes the headers.

## Consequences

- `ce::X` is v2. `ce::v1::X` is what v0.3.0 published.
- A shared entity is one type through every spelling, so a codec written for v0.3.0 serves
  both generations.
- Two event models are maintained for as long as `ce::v1` exists. Retiring it would itself
  remove published declarations, so it is a decision for its own change request.
- The v1 headers carry no prose, like every other header, and are outside the clang-tidy
  gate (`D-TIDY-4`).

## References

- `docs/SPEC.md` section 3 rule 4
- `spec/requirements/change-request/CR-0002.md`
- `SWR-BUILD-0005`, `SWR-BUILD-0006`, `SWR-BUILD-0011`
- ADR-0003 (the describe seam), ADR-0008 (the v2 event model)
