# ADR-0013: A binary-body opt-in and moving entry points, as additions to a frozen generation

## Status

Accepted 2026-10-21 for the opt-in, on CR-0005.
The moving entry points are **deferred until a measurement** of a non-JSON binary-mode receive and a further owner decision; the decision below about them is a recommendation, not an accepted decision.
It does not change ADR-0012: `ce::v4` and `ce::v3` stay frozen, and this decision adds to v4 only.

## Context

v0.6.0 parses a binary-mode body under a JSON media type (ADR-0012).
A consumer that forwards events and never reads the payload pays that parse: on the HTTP full-event path 15% (RapidJSON) to 54% (nlohmann) more instructions than `ce::v3`, which stores the body unparsed.
On 2026-10-11 the owner ruled "No, leave it" on an option to skip it, and on 2026-10-20 reversed that ruling and approved two additions in principle: an opt-in to skip the parse, and entry points that move a message's body instead of copying it.

v4 is frozen since v0.6.0 (`docs/SPEC.md` section 3 rule 4, `SWR-BUILD-0006`), so each addition must be one, and the line between an addition and a break was drawn by the owner on 2026-09-28: a change to documented behaviour breaks the API, whatever the declarations say.
Three constraints decide the shape.

**`decode_options` is one type shared by v3 and v4.**
It is declared in `ce::v3::json` and brought into `ce::v4::json`, and `test/build_test.cpp` asserts the two are the same type.
A v4-only struct would replace a published v4 declaration.

**v3 takes no options where it reads a binary body.**
The v3 `from_message` and `from_payload` have no `decode_options` parameter, and the v3 `read_body` stores a JSON body as unparsed `json_text`.

**`message::body` stays `std::vector<std::byte>`** (CR-0004 item 4), while `json_text::raw` is a `std::string`.
A move can therefore only reach the `ce::binary` alternative.

## Decision

**`decode_options` gains a trailing member `bool parse_binary_body = true`.**
At the default every behaviour is today's.
With `false`, the binary-mode body reader stores a non-empty body under a JSON media type as `json_text` holding the body's bytes exactly as received, without parsing or validating it, whatever its size and the retention limit; it never fails with `parse_error`.
An empty body still carries no payload, and a body under another media type is still carried as bytes.
The member is last and has a default member initializer, so a designated initializer that names only `retain_document_up_to`, a positional initializer and `{}` keep compiling, warning-free under `-Wextra -Werror` on GCC and clang.

**Only the binary-mode body reader honours it.**
Structured decode, `http::from_batch_message`, `nats::from_payload` and the `json_format` decoders accept the struct and ignore the field, because they read JSON text that has to be parsed to be an event.
`json_format::from_value` and `from_value_as` take no options and are unchanged.
v3 ignores the field: its binary entry points cannot receive options, and its reader already behaves as `false` would make v4's.
The field's doc comment says so.

**The member is an addition under rule 4 (the owner ruled so on 2026-10-21).**
The documentation states the 16 KiB default and the designated-initializer spelling, and `D-JSON-5` says the aggregate "gains members without breaking a call".
It states nothing about the struct's arity or decomposition, and no behaviour a program can observe changes at the default.
The one source form that stops compiling, `auto [limit] = options;`, is documented nowhere and used nowhere in the tree.
CR-0005 gives the counter-argument and the evidence.
The owner accepted the consequences: `decode_options` grows from 8 to 16 bytes, `auto [limit] = options;` stops compiling, and v3 ignores the field.
The counter-argument, that the struct definition is itself a published declaration whose size and decomposition a data member changes, was weighed and not adopted.

**The rule this sets for aggregates (owner, 2026-10-21):** a trailing data member with a default member initializer, appended to an aggregate, is an addition under rule 4.
The frozen-API diff job is told the same, so it needs no per-member allowlist entry.
`SWR-BUILD-0006` is not amended by this decision; CR-0005 proposes wording as a follow-up.

**Deferred: the moving entry point has a new name, not an overload (recommended; the owner chose on 2026-10-21 to measure first).**
A same-name `from_message(message&&)` would make `from_message<C>(std::move(m))` move from `m` where today it binds to `const message&` and leaves `m` intact, the class of change the owner ruled a break on 2026-09-28, and would make `&from_message<C>` ambiguous.
`from_message_moving<Codec>(message&&, decode_options = {})` in the HTTP, Kafka and NATS bindings, with `binding::read_body_moving` under them, changes no existing call.
It returns what `from_message` returns, moves the body into a `ce::binary` payload for a non-JSON body, leaves `header_fields` unchanged and the body valid and unspecified after success, and leaves the message unchanged after a failure.
`from_batch_message` and `from_payload` get no moving form: they keep no bytes of the input to move.
v3 gets none, v4 only.
The working name is the owner's to change, and the whole entry point waits for the measurement.

## Consequences

### Positive
- A forwarder of binary-mode events pays no parse and cannot be refused for a body it never reads, with a measured cost near v3's.
- No existing call changes meaning, in either addition.
- v3 is untouched.
- If the moving entry points go ahead, a non-JSON binary-mode body is received without a second copy.

### Negative
- `decode_options` grows from 8 to 16 bytes, and v3 callers of `json_format::decode` can write a field that does nothing.
- A caller who opts in takes over validation of the body.
- The move saves nothing for a JSON body, so the forwarder case gains from the opt-in alone.
- If the moving entry points go ahead, a second name per binding and a longer export list in the module.

### Neutral
- The default stays "validate and parse", and `SWR-BIND-0008` becomes the default behaviour rather than the only one.
- Structured mode is unchanged: a forwarder that needs no parse uses binary mode.
- `ce::v4` stays the inline namespace, and v0.6.1 is a patch release: the owner ruled the opt-in an addition.

## Alternatives considered

- **A v4-only options struct.** It replaces a published v4 declaration. Declined by the owner on 2026-10-20.
- **A separate `from_message_raw`.** It doubles the entry points for one flag. Declined by the owner on 2026-10-20.
- **Validate without keeping the DOM.** The validation is the parse and saves no instructions. Declined by the owner on 2026-10-20.
- **Keeping the parse as a known limitation.** Ruled by the owner on 2026-10-11, reversed on 2026-10-20.
- **A same-name `from_message(message&&)`.** It silently changes the state of a moved argument and makes the function's address ambiguous; recommended against in CR-0005.
- **A tag or wrapper parameter on `from_message`.** It avoids the hazard with one name, at the price of a public tag type and a third-place options parameter; the alternative to the new name if the owner wants one name.
- **`from_message(message)` taking the message by value.** It replaces a published declaration and copies on every lvalue call.

## References

- `spec/requirements/change-request/CR-0005.md`
- ADR-0012 (the frozen generations), ADR-0011 (the perf gates)
- `SWR-BIND-0006` to `SWR-BIND-0013`, `SWR-JSON-0040`, `SWR-BUILD-0006`
- `docs/SPEC.md` section 3 rule 4, `docs/DECISIONS.md` `D-JSON-5`, `D-CORE-11`
