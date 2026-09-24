# ADR-0010: An event keeps its JSON document, in a third generation

## Status

Accepted 2026-09-23, on CR-0003.
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
A copy shares the holder; the reference count is the only state that changes after construction.
The holder carries the identity of the codec that built it: `Codec::identity`, a `static constexpr std::string_view` the codec declares, holding a reverse-DNS name such as `io.cloudevents.cpp.codec.nlohmann`.
`json_document::get<Codec>()` returns `const Codec::value*` when the two identities compare equal by value and `nullptr` otherwise.
Uniqueness is the codec author's contract, as the one-definition rule is; the in-tree codecs live under `io.cloudevents.cpp.`.
Protobuf's `Any` type URLs, LLVM's explicit kind discriminators and COM's interface IDs are the same shape: an identity the type declares rather than one the toolchain derives.

The identity is declared rather than derived because each derived form was worse:
- The address of a per-codec `inline constexpr` tag can collide.
  MSVC's default `/OPT:ICF` folds identical read-only data, and a collision would turn the downcast after the check into undefined behaviour.
- A non-const tag cannot be folded, but was declined: it is mutable global state carried only for its address.
- RTTI was declined as a design smell and an extra dependency; a `-fno-rtti` build would lose the fast path.
- A hash of the name from `__PRETTY_FUNCTION__` gives false matches, because codecs of the same name in two anonymous namespaces spell the same.

**Equality goes through the codec.**
Two documents with the same identity compare with `Codec::equal`.
Two with different identities are compared by the left-hand codec: it parses the right-hand document's serialisation and applies its own `equal`.
Each holder can serialise and parse because it knows its codec.
Comparing compact serialisations instead would report a difference in member order as inequality.

**The v3 codec concept requires `equal`, `copy`, `extract`, `for_each_mutable_element` and `identity`.**
Encoding copies the DOM into the output document, equality needs the codec's own comparison, and the fast path needs the identity.
The codec supplies the copy, because RapidJSON's value cannot be copy-constructed and copies only through its allocator.
Decoding moves the `data` member out of the document it parsed with `extract(value& object, std::string_view key) -> value`, because the decoder owns that document and `find` only reaches a member through a pointer to const.
A deep copy was measured instead, on 2026-09-24: it took up to 187 percent longer than dumping the member to text (Boost.JSON) and raised allocations from 13 to 4015 (nlohmann, a 52889-byte event), while a move allocates nothing.
A caller extracts only a member `find` has returned; a codec given an absent key returns a null value and leaves the object unchanged, so a broken precondition is a wrong payload, never undefined behaviour.
Batch decoding owns its parsed array in the same way, so it walks the elements with `for_each_mutable_element(value& array, F visit)`, which hands each element to `visit` as `value&`, and moves each element's `data` out with `extract`.
Copying them instead cost `decode_batch_100/boost.json` 5.48 percent more instructions than main in the CI perf job on 2026-09-24, and the other codecs 1.3 to 1.5 percent.
The traversal has its own name because the const `for_each_element` accepts any visitor: an overload on `value&` would let a codec without one satisfy the concept and fail only inside the decoder.
`from_value` still copies, since it reads a document the caller keeps.
Requiring all five keeps a single code path.
The in-tree codecs gain a static `equal`, `copy`, `extract`, `for_each_mutable_element` and `identity`; adding members keeps their v1 declarations.

**Decoding retains the document up to a limit.**
The JSON format stores `json_document` when the input is at most `retain_document_up_to` bytes (64 KiB by default) and `json_text` above it.
A parsed DOM can occupy many times its text, so the limit bounds what one event can pin in memory.

**Typed payloads read and write the DOM.**
`decode_as`, `decode_batch_as`, `from_value_as` and `encode_as` sit in `format/typed_payload.hpp`, returning `decoded<T>{.event, .payload}` where they return a payload.
`json_format.hpp` stays free of the describe seam.

**Layout, following ADR-0009.**
`include/cloudevents/` holds v3 and the shared headers.
`include/cloudevents/v2/` holds the v2 copies of every header that mentions `event` or `data_t`, in `namespace ce::v2`: `core.hpp`, `format/json_format.hpp`, `format/typed_payload.hpp` and the four binding headers.
They are copied at the freeze, which is v0.4.0 plus fixes that kept its declarations.

`core.hpp` is split first, so the attribute types are not copied.
Everything above `data_t` moves to a shared `attributes.hpp`: `binary`, `uri`, `attribute_value`, `json_text`, the validated attribute types, `spec_version`, the literals and the extension-field traits.
With it, `detail/timestamp.hpp`, `detail/validated_string.hpp`, `message.hpp`, `extensions.hpp`, `format/describe_json.hpp` and `binding/detail/percent.hpp` become shared: declared in `ce::v2`, brought into `ce::inline v3`.
So `ce::v2::id` and `ce::v3::id` are one type, and code of both generations exchanges attributes and messages without conversion.

The v3 codec concept is declared in `ce::v3::json` beside the shared `ce::v1::json` entities, so `format/json_codec.hpp` is not copied.
The v0.4.0 suites, examples and fuzzers are copied to `test/v2/`, `examples/v2/` and `fuzz/v2/`, without `// spec:` markers.

## Consequences

### Positive
- A decode followed by a typed read with the same codec parses the payload once and never serialises it.
- A typed write followed by an encode never produces intermediate payload text.
- Copying an event copies a pointer, not a DOM, and needs no lock to read.
- The identity check makes a codec mismatch a slower path, never undefined behaviour.

### Negative
- Three event models are maintained, and CI runs all three generations.
- A third-party codec must add `equal`, `copy`, `extract`, `for_each_mutable_element` and an `identity` to move to v3.
- An event holding a `json_document` uses more memory than one holding the same text.
- Comparing documents from different codecs serialises one and parses it again.

### Neutral
- `json_text` stays for payload text a caller supplies, and for documents above the retention limit.
- `ce::v2` becomes frozen, and retiring it later is its own change request, as for v1.

## References

- `spec/requirements/change-request/CR-0003.md`
- ADR-0004 (the codec concept), ADR-0008 (the v2 event model), ADR-0009 (two generations side by side)
- `SWR-CORE-0031` to `SWR-CORE-0034`, `SWR-JSON-0039` to `SWR-JSON-0042`, `SWR-EXT-0007` to `SWR-EXT-0012`, `SWR-BUILD-0012`
- `docs/SPEC.md` section 3 rule 4
