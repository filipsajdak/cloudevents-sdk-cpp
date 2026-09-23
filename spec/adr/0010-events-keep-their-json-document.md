# ADR-0010: An event keeps its JSON document, in a third generation

## Status

Proposed 2026-09-23, on CR-0003.
It applies ADR-0009's layout a second time and extends ADR-0004.

## Context

A typed read parses a payload, serialises it and parses it again, because an event can only carry JSON as text.
CR-0003 measures the cost and asks for an event that holds the parsed document instead.
Four constraints decide the shape.

**Core names no codec type.**
`SWR-CORE-0013` keeps `core.hpp` free of any JSON library, so the `CE_DEFAULT_CODEC=OFF` build has no nlohmann.
A DOM therefore has to sit behind type erasure.

**An event is a value.**
Callers copy events into queues and across threads.
A payload that copies its whole DOM on every event copy loses what the change gains.
A payload that is shared must not change after it is built, or every read needs a lock.

**The fast path must be provable.**
A document built by one codec is only readable as that codec's `value`.
Picking the fast path by guessing the type would be undefined behaviour on a wrong guess.

**`data_t` is a published declaration.**
Adding an alternative breaks every exhaustive visitor, so rule 4 puts it in a new generation.

## Decision

**`json_document` is an immutable, shared, type-erased DOM.**
It holds a `std::shared_ptr` to a const, codec-specific holder that is never modified after construction.
The holder carries a codec identity: the address of a per-codec `inline constexpr` tag.
`json_document::get<Codec>()` returns `const Codec::value*` when the identity matches and `nullptr` otherwise.
A copy shares the holder; the reference count is the only state that changes after construction.

**Equality goes through the codec.**
Two documents with the same identity compare with `Codec::equal`.
Two with different identities compare their compact serialisations, which the holder can produce because it knows its codec.

**The v3 codec concept requires `equal` and a copyable `value`.**
Encoding copies the DOM into the output document, and equality needs the codec's own comparison.
Requiring both keeps a single code path.
The in-tree codecs gain a static `equal`; adding a member keeps their v1 declarations.

**Decoding retains the document up to a limit.**
The JSON format stores `json_document` when the input is at most `retain_document_up_to` bytes (64 KiB by default) and `json_text` above it.
A parsed DOM can occupy many times its text, so the limit bounds what one event can pin in memory.

**Typed payloads read and write the DOM.**
`decode_as`, `decode_batch_as`, `from_value_as` and `encode_as` sit in `format/typed_payload.hpp`, returning `decoded<T>{.event, .payload}` where they return a payload.
`json_format.hpp` stays free of the describe seam.

**Layout, following ADR-0009.**
`include/cloudevents/` holds v3 and the shared headers.
`include/cloudevents/v2/` holds the v2 copies of every header whose declarations change, restored from the v0.4.0 tag in `namespace ce::v2`.
That is every header that mentions `event` or `data_t`, and `format/json_codec.hpp`, whose concept changes.
The v0.4.0 suites, examples and fuzzers move to `test/v2/`, `examples/v2/` and `fuzz/v2/`, without `// spec:` markers.
Entities unchanged since v0.4.0 are brought into `ce::inline v3` by using-declarations, as v2 does with v1.

## Consequences

### Positive
- A decode followed by a typed read with the same codec parses the payload once and never serialises it.
- A typed write followed by an encode never produces intermediate payload text.
- Copying an event copies a pointer, not a DOM, and needs no lock to read.
- The identity check makes a codec mismatch a slower path, never undefined behaviour.

### Negative
- Three event models are maintained, and CI runs all three generations.
- A third-party codec must add `equal` to move to v3.
- An event holding a `json_document` uses more memory than one holding the same text.
- Comparing documents from different codecs serialises both.

### Neutral
- `json_text` stays for payload text a caller supplies, and for documents above the retention limit.
- `ce::v2` becomes frozen, and retiring it later is its own change request, as for v1.

## References

- `spec/requirements/change-request/CR-0003.md`
- ADR-0004 (the codec concept), ADR-0008 (the v2 event model), ADR-0009 (two generations side by side)
- `SWR-CORE-0031` to `SWR-CORE-0034`, `SWR-JSON-0039` to `SWR-JSON-0042`, `SWR-EXT-0007` to `SWR-EXT-0012`, `SWR-BUILD-0012`
- `docs/SPEC.md` section 3 rule 4
