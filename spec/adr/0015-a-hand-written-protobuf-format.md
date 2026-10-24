# ADR-0015: A hand-written Protobuf format, numbered from the .proto

## Status

Accepted 2026-10-24 for the four owner rulings of CR-0009: the bet goes ahead as an addition to `ce::v4`, the codec is hand-written, text data is written to `text_data` and read from any data field, and the field numbers follow `cloudevents.proto`.
The mapping questions that CR-0009 lists as open (limits, timestamps, empty data, JSON classification, `proto_data`) are not decided here.
ADR-0014 belongs to another change request; the gap is intended.

## Context

`docs/SPEC.md` section 1 lists the Protobuf format as out of scope and "design must not block it".
CR-0006 ranked it and proposed a bet; the owner accepted the bet on 2026-10-24, and the spike (pull request 113, `docs/spikes/protobuf.md`) measured it: the Go and Java SDKs agree on the schema and on every attribute type, no published declaration needs to change, and a hand-written reader and writer of 455 formatted lines (326 as written) decodes the bytes of both SDKs.

Three facts decide the shape.

**The `.proto` and the prose of the format disagree, and the wire follows the `.proto`.**
`protobuf-format.md` numbers the attribute map 1 and the data fields 2 to 4.
`cloudevents.proto` at `v1.0.2`, which both SDKs implement, numbers them 5 and 6 to 8.
The spike decoded the bytes of both SDKs with `protoc` against the `.proto`.

**The core has no dependency except CTRE** (`CLAUDE.md`, `docs/SPEC.md` section 4).
The Protobuf wire form of this one fixed schema is small: varints, length-delimited fields, a map entry, two oneofs, `Timestamp` and `Any`.
The depth is fixed (event, map entry, value, `Timestamp`), and `Any` is opaque.

**`v4` is frozen** (`SWR-BUILD-0006`).
A format is a new entry point, so it can be an addition, as `json_format` was for the bindings.

## Decision

**The Protobuf format ships as an addition to `ce::v4`**: a new header `cloudevents/format/protobuf_format.hpp` and a new header-only target `ce::format_protobuf` that links `ce::core` only.
No published declaration changes.
`data_t` does not gain an alternative; a faithful slot for a `proto_data` payload would change a published `std::variant` alias and every exhaustive visitor, which is a break, and no case found by the spike needs it.

**The codec is hand-written.**
There is no `libprotobuf`, no generated code and no `protoc` at build time.
The core stays CTRE-only, the format target links the core and nothing else, and the dependency wording that CR-0006 proposed for a format with a third-party library does not change.
Because the decoder parses untrusted bytes, a libFuzzer target exists from the first slice (`SWR-PROTO-0017`), and the limits of sizes and counts are named constants (`SWR-PROTO-0010`).

**The field numbers are those of `cloudevents.proto` at `v1.0.2`**, not those of the prose: `id` 1, `source` 2, `spec_version` 3, `type` 4, `attributes` 5, `binary_data` 6, `text_data` 7, `proto_data` 8.
The discrepancy is reported to the owner as the rule 5 of `CLAUDE.md` requires, and recorded in `docs/DECISIONS.md` by the implementing stage.

**Text is written to `text_data` and every data field is read.**
A `std::string` payload and a JSON payload are written to `text_data`, which follows the specification and the Java SDK; a `binary` payload to `binary_data`.
The decoder accepts `binary_data`, `text_data` and `proto_data` from either SDK.
The Go SDK never writes `text_data`, and it returns a `text_data` payload of `application/json` as a JSON-quoted string; that is a known limitation and an interoperability finding, recorded the way D-INTEROP-5 records the findings of the Kafka records, and the recorded bytes are not edited to hide it.

**Decode errors use the existing `errc` enumerators** (`SWR-PROTO-0009`); none is added.

## Consequences

### Positive
- The core gains a second event format without a dependency, a generator or a compiled library; the format is a header.
- An event written by the Go or Java SDK is read here, and the reverse, to the extent the interoperability evidence of `SWR-PROTO-0016` shows.
- The encoder is deterministic (`SWR-PROTO-0011`), which neither SDK is.

### Negative
- A hand-written reader of untrusted bytes has none of the years of fuzz and sanitizer hardening of `libprotobuf`; the fuzz target and the limits are the answer, and they are owed from the first slice.
- The code is ours to maintain: about 455 to 480 lines, a test file, a fuzz target and an interoperability harness (Go and Java).
- A timestamp loses its offset and fraction digits, and its range is that of `ce::timestamp`, roughly the years 1678 to 2262, narrower than the wire's years 1 to 9999.
- An event with a JSON `data` is read back as `std::string` or `binary` unless CR-0009 open question 4 is ruled otherwise.

### Neutral
- Structured mode in the bindings stays hard-wired to JSON until the second slice, which adds new entry points under new names.
- `ce::v3`, `ce::v2` and `ce::v1` are untouched.

## Alternatives declined

- **`libprotobuf` with the spike's numbers.**
  It brings the first compiled library into a header-only project (`docs/SPEC.md` section 4, `SWR-ADOPT-0001`), needs `protoc` and a runtime of the same version, two packaging ports and a rewording of the dependency constraint (CR-0006).
  Its generated code for the one schema is 2,129 lines of header and 2,048 of source.
  Declined by the owner on 2026-10-24.
- **The numbers of the prose.**
  They are not on the wire in either SDK; a message written with them would not decode there.
- **A `proto_any` alternative in `data_t`.**
  Faithful, and a break (a changed `std::variant` alias).
  Not needed by any spiked case.
- **Wrapping the Protobuf payload in an extension attribute.**
  It would invent a CloudEvents attribute.

## References

- CR-0009, CR-0006, CR-0005 (additions in a frozen generation), ADR-0005, ADR-0008, ADR-0012, ADR-0013.
- `SYS-PROTO-0001`, `SWR-PROTO-0001` to `SWR-PROTO-0018`; `SWR-BUILD-0006`, `SWR-SEC-0001`, `SWR-SEC-0008`.
- Pull request 113, `docs/spikes/protobuf.md` and `interop/protobuf-spike/`.
- `cloudevents/spec` at `v1.0.2`: `cloudevents/formats/cloudevents.proto`, `cloudevents/formats/protobuf-format.md`.
