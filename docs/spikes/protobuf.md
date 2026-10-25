# Spike: the CloudEvents Protobuf format (CR-0006)

Status: evidence, not a requirement. Run on 2026-10-22 for the bet in
[CR-0006](../../spec/requirements/change-request/CR-0006.md), "Recommended bet".
The decision date in that bet is 2026-11-11. This report does not edit CR-0006; the owner decides.
No library code was written. The prototype codec measured in section 5 lives outside the repository;
only the generators and the goldens are committed, under `interop/protobuf-spike/`.

> **Moved.** CR-0009 stage 1 moved them: the generators are in `interop/protobuf/` (Go, Java and
> `protoc/gen_edge.py`) and the goldens in `test/fixtures/interop/protobuf/`. The generators gained
> a `verify` mode and lost the spike's `dump` and `nondet` modes. The "Reproduce" commands below
> are the spike's own and name the old paths.

## Verdict

**The two SDKs agree on the schema and on every attribute, but not on the bytes and not on the
data field, so the success signal is met in part: no break is needed, `proto_data` has a
representation that is an addition, and a hand-written reader and writer measured 455 formatted
lines and round-trips both SDKs' bytes.**
The same facts also trip the pivot signal read literally ("the two SDKs disagree on a v1.0.2 MUST"):
sdk-go never writes `text_data`, where the spec says text "MUST" be stored there. See section 3.

| CR-0006 field | Expected | Found |
|---|---|---|
| Riskiest assumption | Go and Java produce bytes that decode to the same event; every attribute and data kind maps with no change to a published declaration; `proto_data` is the hard case | Schema identical in both SDKs and in the spec `.proto`. Bytes identical for 1 of 13 events. Both SDKs decode each other's bytes for all 13; 8 events decode to the same event from both producers in both SDKs, 5 do not (3 are an sdk-go decode defect on `text_data` + JSON, 2 are the `proto_data` type URL). Every attribute type maps. `proto_data` maps as `binary` plus `dataschema` plus `datacontenttype`, lossy only when type URL and `dataschema` differ |
| Smallest test | Goldens from both SDKs for about ten events, plus the list of cases that do not map | 13 events from each SDK, 13 hand-built wire cases from `protoc`, one batch; 40 golden files; the unmapped list is section 4 |
| Success signal | Both SDKs agree (byte-equal or decode-equal); no case needs a break; `proto_data` has an additive representation; a measured line count | Decode-equal for 8 of 13, differences written down (section 3); no break found; `proto_data` additive; measured 455 lines formatted (326 as written) for encode plus decode of one event, without batch |
| Stop or pivot signal | The SDKs disagree on a v1.0.2 MUST, or a break is needed | No break. One MUST is read differently: text data goes to `text_data` (Java) or `binary_data` (Go). Wire-compatible, so the owner can read this as an sdk-go defect rather than a disagreement; the signal is the owner's call |
| Decision date | 2026-11-11 | Unchanged |

**Recommendation: continue, with a hand-written codec and the narrowed first slice of section 6,
unless the owner reads the `text_data` difference as the pivot signal.**

## 1. The spec as pinned

Sources, fetched at the tag: 
`https://raw.githubusercontent.com/cloudevents/spec/v1.0.2/cloudevents/formats/cloudevents.proto` and
`https://raw.githubusercontent.com/cloudevents/spec/v1.0.2/cloudevents/formats/protobuf-format.md`.
The `.proto` last changed upstream on 2022-01-18 (a C# namespace option), so no later change is
expected to move field numbers.

**The `.proto` and the prose of `protobuf-format.md` disagree, and the wire follows the `.proto`.**
CR-0006 quotes the prose numbers; they are not on the wire.

| | `cloudevents.proto` (what both SDKs implement) | `protobuf-format.md` prose |
|---|---|---|
| required attributes | `id`=1, `source`=2, `spec_version`=3, `type`=4 | not numbered |
| attribute map | `map<string, CloudEventAttributeValue> attributes = 5` | `= 1` |
| data oneof | `binary_data`=6, `text_data`=7, `proto_data`=8 (`google.protobuf.Any`) | 2, 3, 4 |
| attribute oneof | `ce_boolean`=1, `ce_integer`=2 (int32), `ce_string`=3, `ce_bytes`=4, `ce_uri`=5, `ce_uri_ref`=6, `ce_timestamp`=7 | `ce_binary`, `ce_uri_reference` |
| batch | `CloudEventBatch { repeated CloudEvent events = 1; }` | same |
| media type of the data | not stated | section 1.2 says `application/protobuf`, section 4 says `application/cloudevents+protobuf` for transports |

Per CLAUDE.md rule 5 this is a discrepancy to report. Nothing in the repository reads the prose
numbers yet. `CloudEventAttributeValue` is nested inside `CloudEvent` in the spec `.proto` and in
sdk-java, and top level in sdk-go; the wire is the same.

MUSTs that bear on encode and decode (all from `protobuf-format.md`):

1. A protobuf message as data MUST be stored in `proto_data`; `datacontenttype` MAY be `application/protobuf`; `dataschema` SHOULD be the type URL.
2. Text data MUST be stored in `text_data`; `datacontenttype` SHOULD carry the media type.
3. Binary data MUST be stored in `binary_data`; `datacontenttype` SHOULD carry the media type.
4. Transports with content identification MUST use `application/cloudevents+protobuf`; the batch is a separate capability with `application/cloudevents-batch+protobuf` and "MUST only be used where supported".
5. Required attributes are explicit fields; optional and extension attributes go in the map, keyed by name (`datacontenttype`, `dataschema`, `subject`, `time` included).
6. Type mapping: Boolean to bool, Integer to int32, String to string, Binary to bytes, URI and URI-reference to string, Timestamp to `google.protobuf.Timestamp`.

The text does not say what "text" is (rule 2) and does not say what to do when `Any.type_url` and
`dataschema` differ (rule 1).

## 2. Goldens

Versions: Go 1.27.1, `github.com/cloudevents/sdk-go/v2` v2.15.2 (the version `interop/go/go.mod` pins) and
`github.com/cloudevents/sdk-go/binding/format/protobuf/v2` v2.15.2 (protobuf-go v1.26.0); JDK 26.0.2.1
compiling `--release 17`, `io.cloudevents:cloudevents-protobuf` 2.5.0 (the version family of
`interop/java/pom.xml`; protobuf-java 3.15.0); `protoc` libprotoc 36.2 for the third producer.
No container was started. Maven was run from an existing unpacked distribution.

13 events, named as the HTTP and Kafka corpus where one exists: `minimal`, `minimal_relative`, `full`,
`extensions`, `text_data`, `text_data_empty`, `binary_data`, `time_nanoseconds`, `extension_types`
(string, int, bool, negative, zero, int32 max and min, binary, absolute URI, timestamp), `unicode`,
`data_array`, `proto_data` and `proto_data_noschema` (a `google.protobuf.Struct` in an `Any`, with and
without `dataschema`). Thirteen more cases are built by `protoc --encode` from the spec `.proto`
(a third producer): `text_data_wire`, `attr_unset`, `duplicate_keys`, `unknown_field`,
`spec_version_03`, `missing_id`, `time_as_string`, `subject_as_integer`, `uri_ref_extension`,
`timestamp_negative_nanos`, `timestamp_year_9999`, `timestamp_year_0001`,
`proto_data_unregistered_type`, plus `batch_two` (a two-event `CloudEventBatch`).
Neither SDK has a batch type (sdk-go's `pb` and sdk-java's jar contain no `CloudEventBatch`), so the
batch is `protoc` only.

The goldens are deterministic: generating again gives identical bytes. For sdk-go this needs
`proto.MarshalOptions{Deterministic: true}` on the message returned by `ToProto`; the SDK's own
`Marshal` writes the map in Go's random order, and one 10-attribute event produced 200 different
byte strings in 200 calls.

How each SDK carries `proto_data` (the `google.protobuf.Struct` case):

| | sdk-go | sdk-java |
|---|---|---|
| write | the caller sets `datacontenttype` to `application/protobuf`; the data bytes go into `Any.value` and `Any.type_url` is **copied from `dataschema`** (empty if there is none) | the caller wraps a message in `ProtoCloudEventData`; `type_url` comes from the message (`type.googleapis.com/google.protobuf.Struct`), whatever `dataschema` says |
| read | `Any.value` becomes the event data; `type_url` is **dropped**; `datacontenttype` is set to `application/protobuf` | the data is the whole serialized `Any` (type URL included) |

## 3. Comparison of the two SDKs

Sizes are in bytes (reproduce with the commands under "Reproduce"):

| event | go | java | bytes identical | why they differ |
|---|---|---|---|---|
| binary_data | 108 | 108 | yes | |
| minimal | 59 | 57 | no | sdk-go always writes `binary_data` (empty when there is no data) |
| minimal_relative | 59 | 57 | no | same |
| extensions | 189 | 187 | no | same, plus map order |
| extension_types | 267 | 265 | no | same, plus map order |
| time_nanoseconds | 78 | 76 | no | same |
| text_data | 104 | 104 | no | go `binary_data`, java `text_data` (same length) |
| text_data_empty | 92 | 92 | no | same |
| data_array | 107 | 107 | no | `application/json`: go `binary_data`, java `text_data` |
| full | 232 | 232 | no | same |
| unicode | 174 | 166 | no | same, plus source (below) |
| proto_data | 254 | 254 | no | a relative `dataschema` is `ce_uri` in go and `ce_uri_ref` in java |
| proto_data_noschema | 159 | 203 | no | java adds the type URL it takes from the message |

After normalising the order of the map entries (`protoc --decode` against the v1.0.2 `.proto`), the
differences are exactly these, and no others:

1. **Data field.** sdk-go has no code path that writes `text_data`. It writes `binary_data` for everything but `application/protobuf`. sdk-java writes `text_data` when the media type starts with `text/` or equals `application/json` or `application/xml` or ends with `+json` or `+xml`, else `binary_data` (read from the bytecode of `ProtoSupport`). Under MUST 2, sdk-go is the one that deviates for `text/plain`; under MUST 3 sdk-java's `application/json` choice depends on reading JSON as text, which the spec does not define.
2. **No data.** sdk-go writes an empty `binary_data` (field 6, length 0); sdk-java writes no data field. A reader cannot tell "no data" from "empty binary data" from sdk-go's bytes.
3. **Map order.** sdk-go: random per call. sdk-java: stable per run, not sorted. Protobuf does not define an order, so no byte comparison of a map is meaningful.
4. **URI versus URI-reference.** sdk-go writes `ce_uri` for `dataschema`. sdk-java picks `ce_uri` if the value is absolute and `ce_uri_ref` if not, and on reading it presents a `ce_uri_ref` extension as a plain URI (the `uri_ref_extension` case): sdk-java does not preserve the URI and URI-reference distinction for extensions.
5. **Timestamps.** Both write `Timestamp{seconds, nanos}` and read it back to the same instant. Both lose the offset: `2026-09-20T12:34:56.123456789+02:00` comes back as `...10:34:56.123456789Z` from both SDKs.
6. **Integers, booleans, binary, strings, `int32` limits.** Identical in both SDKs for all values tried, including 2147483647 and -2147483648.
7. **`proto_data`.** See section 2. Different type URL and value handling; the `Any.value` bytes are the sender's own serialization (the Struct map order is the sender's; the goldens insert the keys in sorted order so that this is not what is compared).
8. **`source`.** On reading, sdk-go parses the source as a URL and writes it back percent-encoded: `/interop/protobuf/` plus "event" with accented e (U+00E9) becomes `/interop/protobuf/%C3%A9v%C3%A9nement`. sdk-java keeps it.

Cross-decoding, each SDK reading its own bytes and the other's (dump of id, source, type, typed
attributes in name order, and data as hex):

| reader | reads sdk-go bytes | reads sdk-java bytes |
|---|---|---|
| sdk-go | all 13 | all 13, but `data_array`, `full`, `unicode` come back with the JSON **quoted as a string** (`"{\"count\":3,...}"`), and `proto_data` loses its relative `dataschema`: sdk-go asserts `types.URI` on the value and drops a `ce_uri_ref` without an error |
| sdk-java | all 13; `proto_data*` data includes the `Any` wrapper | all 13 |

The sdk-go quoting is a defect on the reading side: for a `text_data` field it calls `SetData(contentType, string)`,
and with `application/json` that encodes the string as a JSON string. A spec-conformant `text_data`
JSON payload from any producer is mangled by sdk-go.

Edge cases from `protoc`, as read by each SDK and by the prototype:

| case | sdk-go | sdk-java | prototype |
|---|---|---|---|
| unknown field 99 | skipped | skipped | skipped |
| duplicate map key | last wins | last wins | last wins |
| `spec_version` "0.3", empty `id` | accepted | accepted | refused (as `ce::spec_version` and `ce::id` do) |
| attribute value with no oneof set | error | event without that attribute | refused |
| `time` sent as `ce_string` | silently zero time | parsed as a timestamp | refused |
| `subject` sent as `ce_integer` | silently empty | error | refused |
| `Timestamp.nanos` = -5 | accepted | accepted | refused |
| Timestamp year 0001 and 9999 | accepted | accepted | refused (outside the `sys_time<nanoseconds>` range, about 1677 to 2262) |
| text_data written by protoc | read | read | read |

Neither SDK validates on decode; the C++ model does, as its JSON decode does.

## 4. Mapping onto the current model

Checked against `include/cloudevents/core.hpp`, `attributes.hpp` and `detail/timestamp.hpp` as they
stand (`ce::v4`). Only the prototype exercised these, not library code.

| CloudEvents item | Protobuf form | Holds losslessly? | Needs |
|---|---|---|---|
| Boolean, Integer, String, Binary | scalar oneof arms | yes (`bool`, `int32_t`, `std::string`, `binary`) | nothing |
| URI, URI-reference | `ce_uri`, `ce_uri_ref` | yes for ours. Read: accept either arm for a URI-valued attribute (sdk-java chooses by absoluteness) | an addition (reader rule) |
| Timestamp | `Timestamp{seconds, nanos}` | the instant, yes. `ce::timestamp` also keeps `offset`, `offset_form` and `fractional_digits` (SWR-CORE-0007), which Protobuf cannot carry: a decoded event equals the original only when it was UTC | an addition: document that decode gives UTC and a digit count of the nanosecond value; not a break |
| Timestamp range | years 1 to 9999 | no: `sys_time<nanoseconds>` spans about 1677 to 2262; the prototype refuses the rest | not new: a limit of the existing type. Widening it would be a break. Whether the JSON decoder has the same limit was not tested here |
| context `datacontenttype`, `dataschema`, `subject`, `time` | map entries by name | yes | nothing |
| extensions | map entries by name | yes; names outside `[a-z0-9]+` are refused as the JSON decode does | nothing |
| data: `std::string` | `text_data` | yes | nothing |
| data: `binary` | `binary_data` | yes | nothing |
| data: `json_text`, `json_document` | `text_data` (java) or `binary_data` (go) | write: yes (`raw` or `dump()`). Read: the format returns `std::string` or `binary`; JSON classification by media type (`is_json_content_type`) is an optional step | an addition |
| data: `std::monostate` | no field (java) or empty `binary_data` (go) | write: yes. Read: `binary_data` of length 0 is ambiguous. A rule is needed, for example "empty `binary_data` with no `datacontenttype` is no data" | a decision for a requirement; not a break |
| data: `proto_data` | `Any{type_url, value}` | held as `binary` (the `value`), `dataschema` = `type_url`, `datacontenttype` = `application/protobuf`. Information lost: the type URL when `dataschema` is also set and differs, and "this was an Any" when `datacontenttype` was something else. On write, the type URL is `dataschema`, as sdk-go does | an addition. A faithful slot (`proto_any` as a new `data_t` alternative) would change a published `std::variant` alias and every exhaustive visitor: that is a break, and the spike found no case that needs it |
| required `specversion` | field 3, a string | only "1.0" is accepted | nothing |
| batch | `CloudEventBatch` | `std::span<const event>` in and `std::vector<event>` out, as the JSON batch does; not in the prototype | an addition |

**Result: no case needs a change to a published declaration.** The format is a new entry point
(`ce::format_protobuf`-style target or header) next to `json_format`, which is an addition
(CR-0006, "Core fit"). Structured mode in the bindings stays hard-wired to JSON; using the format
through a binding is a later slice.

## 5. Dependency: libprotobuf or a hand-written codec

**libprotobuf.** Facts measured on this machine (Homebrew `protobuf` 36.2, `abseil` 20260817.0):

| item | value |
|---|---|
| license | BSD-3-Clause (the vcpkg manifest says so) |
| availability | vcpkg `protobuf` 7.36.2 (depends on `abseil`, `utf8-range`, and a host `protobuf`); Conan Center versions 7.36.2, 7.35.1, 6.33.5, 5.29.6, 3.21.12 |
| runtime size | `libprotobuf.dylib` 3.07 MB, `libprotobuf-lite.dylib` 0.69 MB; the full library links 79 Abseil dylibs totalling 4.87 MB |
| generated code | `protoc --cpp_out` of the spec `.proto`: 2,129 lines of header and 2,048 of source, 75 KB object, 4.4 s to compile once at `-O1` |
| codegen | `protoc` is needed at build time, and the generated code must match the runtime version exactly (CR-0006 quotes protobuf.dev: no cross-version guarantee); the generated files can be committed, which pins the runtime version instead |
| header-only? | no. The target would be the first compiled library in the project, which `docs/SPEC.md` section 4 and SWR-ADOPT-0001 exclude today |

Not measured: a full libprotobuf build from source, the vcpkg and Conan build time, a binary size
with the library linked in, and the CI jobs a dependency adds.

**Hand-written codec**, prototyped in the scratch directory against `ce::event` with
`ce::result`, C++20, `g++-16 -Wall -Wextra -Wpedantic -Wconversion -Wshadow` clean:

| item | value |
|---|---|
| size | `protobuf_wire.hpp` 326 lines as written, **455 lines** after `clang-format` with the repository style; decode and encode of one event; no batch (about 20 more, the wire form is a repeated field 1), no typed-payload helpers |
| content | a bounds-checked reader (varint at most 10 bytes with an overflow check, length against the remaining buffer, unknown-field skip, groups refused), UTF-8 validation of strings (proto3 requires it), `Timestamp`, `Any`, the map entry, the two oneofs; a writer for the same |
| passes | decodes all 13 sdk-go goldens and 13 sdk-java goldens to the same dump as the SDKs do (the exceptions are the two `proto_data` cases, where the value is the `Any` payload and the type URL is `dataschema`, as in section 4); decodes the `protoc` cases as in the section 3 table; re-encodes every golden it could decode and **both sdk-go and sdk-java decode those bytes to the same event as the original** (differences only in the lossy `proto_data` cases) |
| output | encoding is deterministic (map entries sorted by name), unlike sdk-go's default |
| smoke fuzz | 1,000,000 mutated inputs (byte flips, truncations, insertions, splices of the goldens) under ASan and UBSan with `-fno-sanitize-recover`: 110,916 decoded, 889,084 refused, no report. This is a smoke run, not a libFuzzer campaign |

Fuzz exposure is the same list either way: a length prefix against the buffer, varint overflow, the
`int32` range, duplicate map keys, an `Any` with opaque bytes, invalid UTF-8, and out-of-range
`Timestamp`. The schema is flat (nesting depth 3, no recursion), so a hand-written decoder has no
depth bound to get wrong. libprotobuf has had fuzz and sanitizer hardening for years; the
hand-written codec has none beyond a new fuzz target, which `SWR-SEC-0001` would need to name.

Reading for the owner: the size of the hand-written path is of the order of 0.5k lines plus tests,
inside the 0.4k to 0.8k range CR-0006 gives for one binding on the shared core, and it avoids the
wording change in the dependency table of CR-0006. The cost of libprotobuf is not the code (the
glue is smaller) but the first compiled dependency, the version-matched codegen and two packaging
ports.

## 6. Verdict and size

- **Both SDKs agree?** On the schema, the attribute types and decoding each other's bytes: yes. On bytes: no (1 of 13). On the data field for text and for "no data": no. Decode-equal for 8 of 13 events; the remaining 5 are an sdk-go decode defect (3) and the type URL of `proto_data` (2).
- **A break needed?** No. Every difference is a reader rule or a documented loss; no published declaration has to change.
- **Size, measured:** one hand-written codec of about 455 to 480 lines (with batch); the earlier estimate of "M to L" in CR-0006 comes down to **M** without a dependency. Files: one header, one test file, one fuzz target, one interop harness (Go, Java and a C++ producer, as in PR #95, which was 2,767 lines mostly harness). New requirements: an estimate, not measured, of 12 to 16 (system: the format and its media types; software: one per attribute kind, one per data kind, the `proto_data` rule, the empty-`binary_data` rule, the timestamp rule, the batch, strictness, the reader limits; one `delivered_in`).
- **Recommendation: continue**, narrowed:
  1. Spec first: a requirement for the wire schema taken from the `.proto` (not the prose), written as a reader that accepts every form in section 3 and a writer that picks one.
  2. Writer choices to put in the requirements, for the owner to rule on: `text_data` for `string`, `json_text`, `json_document`, `binary_data` for `binary`, no data field for no data, `ce_uri` for `dataschema`, sorted map order, `proto_data` only for `binary` with `application/protobuf`. This follows MUST 2 and so does what sdk-java does and what sdk-go does not.
  3. The first slice is the format only (`bytes <-> event`, plus batch), the hand-written codec, one fuzz target, goldens from both SDKs and Go and Java readers of what C++ writes.
  4. Report the sdk-go `text_data` + JSON defect and the `text_data` omission upstream; neither is tracked here.
- **Pivot to MQTT?** Only if the owner reads MUST 2 as a disagreement between the SDKs that blocks agreement. The reading here is that the bytes are wire-compatible and one SDK deviates, which is the shape D-NATS-1 already accepted.
- **Decision date:** 2026-11-11, unchanged.

## Not verified

- sdk-go and sdk-java were run at one version each (v2.15.2 and 2.5.0); later versions were not run (sdk-go has v2.16.2, sdk-java 5.1.0). Whether either changed `text_data`, the `Any` handling or the quoting was not read.
- sdk-java's text rule was read from `javap` output of the jar, not from source.
- Whether JSON decode has the same year range limit as the Protobuf timestamp refusal.
- The libprotobuf build cost from source, the packaging ports, and the CI cost of a dependency.
- No C++ batch codec was written; the batch bytes are `protoc` only.
- Behaviour on 32-bit or big-endian hosts (the prototype uses byte shifts, not memcpy of integers, but it was run on arm64 only).
- A libFuzzer or long-running fuzz campaign.

## Reproduce

Generators and goldens: `interop/protobuf-spike/`. Everything below ran outside containers.

```sh
# the pinned spec files
curl -fsSL -o /tmp/spec/cloudevents.proto \
  https://raw.githubusercontent.com/cloudevents/spec/v1.0.2/cloudevents/formats/cloudevents.proto

# sdk-go (go 1.27, GOFLAGS=-mod=mod on first run)
(cd interop/protobuf-spike/go && go build -o /tmp/pbspike-go . )
/tmp/pbspike-go gen    /tmp/out/go            # goldens/go
/tmp/pbspike-go dump   /tmp/out/java /tmp/dump/go-reads-java
/tmp/pbspike-go nondet                        # 200 SDK Marshal calls, distinct encodings

# sdk-java (maven 3.9.9 or javac with the jars of the pom on the class path)
mvn -q -f interop/protobuf-spike/java/pom.xml compile exec:java -Dexec.args="gen /tmp/out/java"
mvn -q -f interop/protobuf-spike/java/pom.xml exec:java -Dexec.args="dump /tmp/out/go /tmp/dump/java-reads-go"

# third producer: protoc, from the spec .proto
python3 -I interop/protobuf-spike/gen_edge.py /tmp/spec /tmp/out/protoc

# independent decode of any golden against the spec schema
protoc -I /tmp/spec --decode=io.cloudevents.v1.CloudEvent /tmp/spec/cloudevents.proto < interop/protobuf-spike/goldens/java/full.pb
```

The prototype codec and its smoke fuzz are not committed (CR-0006: "a throwaway reader"). Its
line counts above were taken from `wc -l` and from `clang-format --style=file` with this
repository's `.clang-format`.
