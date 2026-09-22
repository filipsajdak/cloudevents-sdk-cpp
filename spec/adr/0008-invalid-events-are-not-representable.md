# ADR-0008: An invalid event is not representable, and `validate()` goes away

## Status

Accepted 2026-09-21. Reverses `docs/SPEC.md` section 9 decision D1, on CR-0001.

## Context

D1 settled on a public aggregate with `validate()` as the gate. The 2026-09-21 hardening
audit found that the gate is advisory in practice: `ce::event e{.id = "", .source = "",
.type = ""};` compiles, an event can be invalidated after passing `validate()` by assigning
to a public member, and two of the four encode paths call `validate()` defensively while
two do not. CR-0001 records the six observations in full.

Three constraints shape what can replace it.

**A `consteval` constructor cannot exist on a type that owns a `std::string`.** Measured on
this machine with GCC 16 and Clang 23: the constructor is rejected as "not a constant
expression because it refers to an incompletely initialized variable", and it is rejected
even when the member is merely default-constructed, because libstdc++'s small-string
pointer points into the object under construction. So the compile-time check cannot live on
the attribute type itself.

**A `std::string` cannot reliably escape constant evaluation.** GCC 16 and Clang 23 both
accept a string short enough for the small-string buffer and reject a longer one. A
`constexpr fail()` would therefore compile or not as a function of how long its message
happens to be, and an edit from fifteen to seventeen characters would break one preset and
nothing else.

**`uri` and `uri_ref` must stay default-constructible.** `describe.hpp` builds `Ext probe{}`
and `event::get<Ext>()` builds `Ext out{}`, and `ce::ext::dataref` holds a `uri_ref`. Giving
those two an invariant breaks the macro backend, the reflection backend and the parity
suite at once.

## Decision

Validity becomes a property of the type, enforced at construction.

**Two families, not one.** Context-attribute types (`id`, `source`, `type`, `subject`,
`datacontenttype`, `dataschema`, `extension_name`) carry invariants and are never
default-constructible. Wire types (`uri`, `uri_ref`), which are alternatives of
`attribute_value` and fields of described extension structs, keep permissive construction,
because the describe seam requires it and CloudEvents does not ask the SDK to parse
RFC 3986.

**Two construction paths, one validator.** Each attribute has one `constexpr` policy
function returning a diagnosis. A runtime factory `T::make(std::string_view)` returns
`result<T>`. A `consteval` proxy type, reachable through a user-defined literal, runs the
same policy at compile time and fails by calling a declared-but-undefined function, so the
diagnostic names the rule and no `throw` is involved. A literal therefore costs no runtime
check, and a runtime value cannot reach the compile-time path: the proxy's only
constructors are `consteval`, so a non-constant argument is ill-formed at the call site.

**A failure type for constant evaluation, not a second result type.** `static_error` holds
an `errc` and two `string_view`s into static storage. A free function, `widen()`, converts
it to `ce::error` and is the single widening point; a converting constructor would have
stopped `error` being an aggregate. There is no `constexpr_result<T>`:
a second `expected`-like alias would need a second polyfill to keep in step, which is the
drift ADR-0002 exists to prevent, and the compile-time path has no value-or-error to carry
because its failure mode is a compile error.

**`validate()` is removed.** Every rule it holds is a single-attribute rule, and CloudEvents
v1.0.2 core defines no constraint spanning two context attributes. Once each part is valid
by construction, the product of valid parts is a valid event, so constructing one from
`(id, source, type, options)` cannot fail. That makes it a constructor rather than a named
factory: a factory returning a result is the idiom for construction that can fail without
exceptions, and the fallible steps (each attribute's `make()`, and `builder::build()`) are
factories already. `lint()` stays: it reports SHOULD-level observations, which are not
validity.

**A builder for decoders.** `binding::read_attributes` and `json_format::from_value`
discover attributes one at a time in wire order and cannot know until the end whether `id`
arrived. `event::builder` accumulates and `build() &&` returns `result<event>`, failing only
on absence: every other refusal happened in an attribute's `make()` before the value reached
the builder. It is rvalue-ref-qualified, so a builder cannot be consumed twice or left
half-built.

## Consequences

### Positive
- An invalid event is unrepresentable rather than detectable, so the question "did anyone
  call `validate()`" stops existing.
- A hardcoded `source` or `type` is checked when the program is compiled, at no runtime
  cost, which the aggregate could not offer at all.
- The `uri`/`uri_ref` implicit conversions and the undefined `const char*` overload
  disappear as a side effect, because the attribute types do not have them.

### Negative
- A breaking API change reaching roughly 360 call sites across the library, suites,
  examples, fuzzers, benchmarks and the interop generator.
- The designated-initializer spelling that D1 chose for its readability is lost. This was
  a real benefit and it is being given up, not argued away.
- The compile-time path is reachable only through a user-defined literal, because
  `f("abc")` where `f` takes `ce::id` needs two user-defined conversions and no arrangement
  removes that. Callers must write `"abc"_id`, and the library's own code must write
  `ce::id{"abc"}` rather than `return "abc"_id;` for the same reason.

### Neutral
- The wire format does not change. `test/fixtures/interop/` and
  `test/fixtures/conformance/` are the regression test for that, and must compare equal
  across the change except where a tightening recorded in its own requirement says
  otherwise.
- `message` keeps holding the permissive header type, renamed `raw_headers`, so the three
  message fuzzers and every fixture keep compiling.

## References

- CR-0001
- `docs/SPEC.md` section 5.1, section 9 decision D1
- ADR-0001, ADR-0002
- `docs/DECISIONS.md` D-CORE-4, D-CORE-7
- CloudEvents v1.0.2 core specification section 3.1
