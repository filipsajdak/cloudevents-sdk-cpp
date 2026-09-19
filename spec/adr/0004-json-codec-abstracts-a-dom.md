# ADR-0004: The codec concept abstracts a JSON DOM, not the event

## Status

Accepted 2026-09-19.

## Context

`docs/SPEC.md` §5.3 requires the JSON layer to work through a pluggable codec so a
consuming project keeps the JSON library it already pins. There are two ways to
draw that seam: abstract the *event* (each codec knows how to turn an event into
text) or abstract the *DOM* (each codec exposes a JSON value type, and one shared
implementation maps events onto it).

Abstracting the event would duplicate every CloudEvents JSON format rule in every
codec, which is precisely the logic that has to be right once.

## Decision

The `json_codec` concept abstracts a JSON DOM: a `value` type, `parse`/`dump`,
constructors for each JSON kind, `set`/`push` mutation, `find`, `kind_of`,
`size_of`, checked `as_*` accessors returning `result`, and callback-based
`for_each_member`/`for_each_element`.

`json_format<Codec>` holds all CloudEvents format logic and names only `Codec::`
statics and core types.

Two details are load-bearing:

- **`find` returns `const value*`.** The format must distinguish *absent* from
  *present-and-null*: `data` absent is not `data: null`, and the
  `data`/`data_base64` mutual-exclusion rule is decided on presence, not value. A
  `result`-returning lookup would conflate absence with error.
- **`as_int` is separate from `as_double`.** The CloudEvents `Integer` type is
  32-bit signed, and `1.0` must not validate as one. A single numeric accessor
  loses that distinction.

Traversal is by callback rather than by iterator so the concept does not encode
nlohmann's iterator shape. `test::mini_codec` is deliberately built the wrong shape
in four ways -- statics only, an order-preserving vector-of-pairs object, strict
integer/double discrimination, and a hand-written parser -- so any nlohmann-ism
that leaks into `json_format` fails to compile against it.

## Consequences

### Positive
- Every format rule is implemented once and tested against two structurally
  different codecs.
- A consumer with a vendored JSON library writes an adapter, not a format.
- No nlohmann type appears outside the nlohmann codec header, which is what makes
  `CE_DEFAULT_CODEC=OFF` meaningful.

### Negative
- `as_string` returns a `string_view` into the codec value, tying its validity to
  that value's lifetime. This would exclude a simdjson on-demand adapter; the
  escape hatch would cost every other codec a copy and is not paid today.
- The `json_text` data alternative costs one parse per encode. See
  `docs/DECISIONS.md` D-JSON-1 for why the zero-copy alternative was rejected.

### Neutral
- The concept is larger than an event-level seam would be, but each requirement
  traces to a specific CloudEvents JSON format rule.

## References

- `docs/SPEC.md` §5.3
- SWR-JSON-0001 through SWR-JSON-0030
- `docs/DECISIONS.md` D-JSON-1, D-JSON-2
