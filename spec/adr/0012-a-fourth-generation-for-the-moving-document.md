# ADR-0012: A fourth generation for the moving document

## Status

Accepted 2026-09-29, on CR-0004.
It applies ADR-0009's layout a third time, after ADR-0010.
It corrects the part of ADR-0010's amendment of 2026-09-27 that called the move of a `json_document` an addition under rule 4.

## Context

v0.5.0 published `ce::v3::json_document` with copy operations only, and its guide stated the consequence: moving a document copies it, so every document holds a DOM.
`main` then gave the document move operations whose source reads as JSON null (`SWR-CORE-0035`).
No declaration changed, so `D-CORE-9` classed the move as an addition.

The owner ruled on 2026-09-28 that a change to documented behaviour is a break, whatever the declarations say.
Rule 4 exists so that a consumer who names a generation keeps what it compiled and tested against; a program that reads a moved-from document after upgrading reads a different value, and nothing in its build says so.
Four constraints decide the shape of the fix.

**v3 must behave as v0.5.0 did, not only declare what it declared.**
The v0.5.0 suite pins the copy on move, so the frozen suites are the evidence, as they are for v1 and v2.

**A frozen generation takes additions and fixes, never a change of behaviour.**
The v3 copies can therefore take main's body-only changes since v0.5.0 (`D-CODEC-3`), and must not take the move.

**Events of two generations must meet.**
A service moving to v4 exchanges events with libraries still on v3.
Converting through the wire format would cost a serialisation and a parse per event, and a DOM copy per document.

**A generation's first tag is the only moment surface can go** (`docs/RELEASE.md`).
Anything v4 should not carry has to leave before v0.6.0.

## Decision

**`ce::inline v4` is the fourth generation, and v0.6.0 is its first tag.**
`include/cloudevents/` holds v4 and the shared headers.
`include/cloudevents/v3/` holds the v3 copies of the seven headers that mention `event`, `data_t` or `json_document`: `core.hpp`, `format/json_format.hpp`, `format/typed_payload.hpp` and the four binding headers, in `namespace ce::v3`, including each other by their `v3/` paths.
They are copied at the freeze, which is v0.5.0 plus the changes on `main` since that keep its declarations and its documented behaviour.
The move of `json_document` is the one change on `main` they do not take.

Everything else stays shared: `attributes.hpp`, `message.hpp`, `extensions.hpp`, `result.hpp`, the describe seam, `format/json_codec.hpp` with the v3 codec concept, `format/describe_json.hpp`, `format/base64.hpp`, the codecs and the binding details.
Each is brought into `ce::inline v4` by using-declarations, as ADR-0009 and ADR-0010 bring shared entities forward, so `ce::v3::id` and `ce::v4::id` are one type and `json::decode_options` is one type.
The v0.5.0 suites, examples and fuzzers are copied to `test/v3/`, `examples/v3/` and `fuzz/v3/`, without `// spec:` markers, and registered outside the clang-tidy gate as `D-TIDY-4` and `D-TIDY-5` describe for v1 and v2.
The module exports `ce::v4` only, for the reason ADR-0009 gives.

**The document model is declared once, in `ce::v3::detail`.**
`json_document_model`, the per-codec holder and the static moved-from model are detail entities of `ce::v3` and are not copied.
`ce::v3::json_document` and `ce::v4::json_document` are two classes over one model: each holds a `std::shared_ptr<const ce::v3::detail::json_document_model>`.
They differ in their special members only: v3 declares copy operations, and v4 declares copy and move operations, with the move leaving the source on the moved-from model (`SWR-CORE-0035`).
Identity checks, `get`, `dump` and equality run through the shared model, so the two generations cannot disagree about what a document holds.

**Conversions are free functions in `ce::v4`.**
`from_v3` and `to_v3` take an event by `const&` or `&&` and return the other generation's event.
The context attributes, the extensions and every `data_t` alternative other than `json_document` are shared types, so they are copied or moved as they are.
A `json_document` converts by copying or moving its model pointer, through one detail accessor both document classes befriend; for `ce::v3::json_document` that friend declaration is an addition.
No conversion can fail, so none returns `result`.
The conversions live in a header of their own rather than in `core.hpp`, so a translation unit using only v4 does not compile the v3 event model.
Its name is for the implementing stage; CR-0004 lists the choice among its open questions.

**v4 `set_data` follows `encode_as`'s media-type rule.**
It keeps a JSON media type, sets `application/json` when none is declared, and refuses any other with `type_mismatch`, leaving the event unchanged.
`set_data`, `event_of::set_data` and `event_of::with_data` return `result`.

**v4 binary-mode receive parses a JSON body.**
The binding core's body reader takes the codec and the decode options.
Under a JSON media type it parses the body with the codec: within the retention limit it stores the parsed document, above it it stores the body's text as `json_text`, and a body that does not parse fails with `parse_error`, exactly as structured decode fails.
`from_message` of each binding takes `json::decode_options` and passes them to both the structured and the binary path, so one call site sets one limit for every mode.

**v4 `json_format` drops seven public helpers** that nothing uses or tests: `required_text`, `optional_text`, `read_required`, `read_optional`, `read_time`, `read_context_attributes` and `read_extensions`.
The private readers they wrapped stay.

**`message` does not change.**
Its `body` stays `std::vector<std::byte>`, and `message` stays one shared type for every generation.

## Consequences

### Positive
- `ce::v3` behaves as v0.5.0 did, and its suites show it.
- `ce::v4` keeps the move without a reference-count operation, and every decode and batch benefits.
- Events cross between v3 and v4 in constant time per document, with no serialisation.
- A malformed JSON body in binary mode is refused on receive, where the sender can still be told.
- v4 starts without surface nobody uses.

### Negative
- Four event models are maintained, and CI runs four generations.
- v4 `set_data` returns `result`, so callers must handle a refusal.
- Binary-mode receive of a JSON body costs a parse it did not cost in v3.
- `ce::v3::json_document` gains a friend declaration for the conversions.

### Neutral
- `json_text` stays for caller-supplied text and for payloads above the retention limit, in both modes.
- `ce::v3` becomes frozen, and retiring it later is its own change request.
- The body type was measured and kept; a `message&&` entry point can still be added to any generation as an addition.

## Alternatives considered

- **A v0.5.1 patch release carrying the move.** A patch cannot change documented behaviour; the owner withdrew v0.5.1 on 2026-09-28.
- **Narrowing rule 4 to declarations.** It would let any behaviour change ship in place, which is the one thing a consumer pinning a namespace cannot detect. Declined by the owner.
- **Reverting the move and staying on v3.** Honours the rule and discards the measured saving (`D-CORE-9`: no atomic operation on a move construction, one fewer on a move assignment). Declined by the owner in favour of a generation that also carries CR-0004's other items.
- **A copy of the document model per generation.** Conversion would then have to copy or re-parse the DOM, since a v3 holder and a v4 holder would be unrelated types.

## References

- `spec/requirements/change-request/CR-0004.md`
- ADR-0009 (two generations side by side), ADR-0010 (the v3 generation and the document)
- `SWR-BUILD-0013`, `SWR-CORE-0035` to `SWR-CORE-0040`, `SWR-EXT-0011`, `SWR-EXT-0013`, `SWR-JSON-0045`, `SWR-BIND-0006` to `SWR-BIND-0009`
- `docs/SPEC.md` section 3 rule 4, `docs/DECISIONS.md` `D-CORE-9`, `D-CODEC-3`
