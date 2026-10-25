# MQTT binding spike (CR-0006)

**Verdict.** The divergence is real but confined to binary mode: sdk-go prefixes every attribute User Property with `ce-` where the v1.0.2 text says names are "used unchanged", while structured mode, the Content Type property, the order of the mode test and the canonical string values agree with the text; our `ce::message` carries an MQTT 5.0 PUBLISH with no change to any published declaration, so the binding is an addition, and a structured-mode first slice needs no ruling on the prefix.

| rule | v1.0.2 text | sdk-go (`mqtt_paho/v2` at `05f0f68`) | our model (sketch) |
|---|---|---|---|
| attribute name in a User Property | "used unchanged" (3.1.3.1) | `"ce-" + name` (`message.go:20`, `write_message.go:97`) | traits prefix `""` plus a filter in the binding, or `"ce-"` as the core stands |
| `datacontenttype` | `Content Type` property (3.1.1); the example in 3.1.4 also lists it as a User Property | `Properties.ContentType` only (`message.go:95-97`, `write_message.go:92`); never a User Property | reserved header `content-type` (as `SWR-KAFKA-0002`); a duplicate User Property is accepted when equal |
| structured or binary | `Content Type` starts with `application/cloudevents` is structured, otherwise binary (3) | same test first (`message.go:43`); otherwise binary only if `ce-specversion` is present (`:45`); neither is `EncodingUnknown` (`:56-63`) | same test; otherwise binary only if `specversion` is present, else `not_a_cloudevent` (as `kafka::from_message`) |
| values | canonical string (3.1.3.2) | `types.Format`, canonical | `render_attribute`, canonical, plus UTF-8 without NUL |
| MQTT 3.1.1 | always structured JSON, no properties (3) | not supported: paho.golang is 5.0 only; sdk-go#1190 open | a `message` with no headers; payload entry points |
| batch | none | none | refused |
| released | - | no tag for `protocol/mqtt_paho`; consumers use a pseudo-version | - |
| second peer | - | sdk-java has none (README row `:x:`, no `mqtt` path in the tree at `81d64ac`) | - |

## Recommendation

1. **Slice 1: structured mode, MQTT 5.0 and 3.1.1.** `to_message` and `from_message` for `application/cloudevents+json`, and `to_payload` and `from_payload` for 3.1.1. The text and sdk-go agree here, so the Go goldens verify it in both directions today (below), with no owner decision. Size S, inside the 170-line sketch.
2. **Slice 2: binary mode, after the owner rules** (next section). It is the only place the SDKs disagree.
3. **Against the Protobuf bet:** MQTT is a valid pivot target if the Protobuf signals stop it, and the prefix question CR-0006 asked to settle first is now settled as a fact, not as a choice. By cost alone it stays first; it has one peer and not two, so its binary mode would have no second opinion. This report did not read the Protobuf spike.

### Decision for the owner: the binary-mode name rule (open question 3)

| | A. the text: no prefix | B. sdk-go: `ce-` | C. the text by default, `ce-` as an opt-in |
|---|---|---|---|
| conforms to 3.1.3.1 (a MUST) | yes | no | yes |
| reads a Go publisher's binary events | no: `not_a_cloudevent` | yes | only with the opt-in |
| a Go subscriber reads ours | no: `EncodingUnknown` | yes | only with the opt-in |
| external check of our binary mode | none (no peer writes it) | one SDK | one SDK, for the opt-in only |
| precedent | D-NATS-1 (the text over sdk-go) | - | D-HTTP-1 (`literal_values`: conformant default, a named opt-in for Go) |

C costs a policy parameter on the two entry points, an addition. Structured mode is the same under all three. A foreign User Property (such as `x-request-id`) is a second ruling, listed under "Open decisions" below.

## 1. What the text requires

Source: `cloudevents/spec` tag `v1.0.2`, `cloudevents/bindings/mqtt-protocol-binding.md` (331 lines), read in full; <https://github.com/cloudevents/spec/blob/v1.0.2/cloudevents/bindings/mqtt-protocol-binding.md>. Line numbers below are that file's.

- **Versions** (`:55-56`, `:135-136`): "The _binary_ mode _only_ applies to MQTT 5.0, because of MQTT 3.1.1's lack of support for custom metadata." "With MQTT 3.1.1, the content mode is always _structured_ and the message payload MUST use the JSON event format."
- **Binary mode on 5.0** (`:62-66`): data is the payload "as-is"; `datacontenttype` goes in the `Content Type` property; "all other event attributes are mapped to User Property fields". `:146-147`: Content Type "MUST be mapped directly to the CloudEvents `datacontenttype` attribute". `:151-152`: the `data` bytes "MUST be used as the payload".
- **Names, no prefix** (`:168-169`): "CloudEvents attribute names MUST be used unchanged in each mapped User Property". The example (`:196-201`) shows `specversion: 1.0`, `type:`, `id:`. There is no `ce-` anywhere in the file.
- **Values** (`:173-175`): "constructed from the respective CloudEvents attribute type's canonical string representation".
- **Extensions** (`:156-158`): "All other ... context attributes, including extensions, MUST be individually mapped to and from the User Property fields".
- **Mode detection** (`:114-119`): the receiver "inspect[s] the `Content Type` property"; prefixed with `application/cloudevents` is structured, "otherwise it defaults to _binary_". `:121-123`: an unknown event format MAY be forwarded as binary. `:125-133` (informative): without a CloudEvents Content Type, a message with all mandatory attributes as properties is "probably a CloudEvent".
- **Structured mode** (`:213-244`): Content Type "MUST be set to the media type of an event format" (5.0); 3.1.1 implies `application/cloudevents+json` and has no Content Type. All implementations that support structured mode "MUST support the JSON event format" (`:72-73`). On 5.0, User Properties are optional in structured mode (`:243-244`, MAY).
- **`data_base64`**: the binding says nothing; structured mode inherits the JSON format, binary mode sends raw bytes.
- **Not specified**: Payload Format Indicator, Topic Name (`:182-183`: chosen by the client), QoS, retain, batch mode.
- **Defects in the text itself**: the example in 3.1.4 lists `datacontenttype` as a User Property as well as the Content Type, which `:62-66` and `:146-147` do not call for; the table of contents swaps 3.1 and 3.2; the structured examples carry invalid JSON (`2018-04-05T03:56;24Z` unquoted). A reader following the example writes a duplicate; the sketch accepts it when it agrees with Content Type.

## 2. What sdk-go does

Read from source, not from a summary. `cloudevents/sdk-go`, module `protocol/mqtt_paho/v2`, at `main` `05f0f6812a369b6950568b0b28788cfb44cc3279` (2026-10-20). Files: [`message.go`](https://github.com/cloudevents/sdk-go/blob/05f0f6812a369b6950568b0b28788cfb44cc3279/protocol/mqtt_paho/v2/message.go) and [`write_message.go`](https://github.com/cloudevents/sdk-go/blob/05f0f6812a369b6950568b0b28788cfb44cc3279/protocol/mqtt_paho/v2/write_message.go); a line is cited as `file:line` and opens at `https://github.com/cloudevents/sdk-go/blob/05f0f6812a369b6950568b0b28788cfb44cc3279/protocol/mqtt_paho/v2/<file>#L<line>`. Both files last changed in `5754cf9` (2024-05-23, ["fix the mqtt contentType issue"](https://github.com/cloudevents/sdk-go/pull/1063), #1063). The tag `v2.15.2` (the version `interop/` pins for the core) still has the older code, described last.

- `message.go:20`: `prefix = "ce-"`. `:23`: `specs = spec.WithPrefix(prefix)`.
- **Reading** (`message.go:39-54`): a message is structured when `Properties.ContentType` starts with `application/cloudevents` (`:43`, `format.IsFormat`); otherwise binary when the User Property `ce-specversion` is present (`:45`). Neither: `ReadEncoding` returns `EncodingUnknown` (`:56-63`). `ReadBinary` (`:76-101`) takes a User Property whose key starts with `ce-` as an attribute (`:82-89`; a name that is not a known attribute becomes an extension), ignores every other key, and reads `datacontenttype` from `Properties.ContentType` (`:95-98`). Matching is exact, so `CE-specversion` is refused (probe below).
- **Writing** (`write_message.go`): structured sets `Properties.ContentType = f.MediaType()` (`:47`) and the payload, with no User Property. Binary writes every attribute and extension as `"ce-" + name` (`:95-106`), the value as `types.Format` text, and `datacontenttype` as `Properties.ContentType` (`:92`). It never sets the Payload Format Indicator.
- **Versions**: the module requires paho.golang, which is MQTT 5.0 only; [sdk-go#1190](https://github.com/cloudevents/sdk-go/issues/1190) (open since 2025-09-18) asks for 3.1.1. There is no tagged release of `protocol/mqtt_paho` (`git ls-remote --tags` finds no `mqtt`; `go list -m -versions` lists none).
- **The older code** ([`v2.15.2`](https://github.com/cloudevents/sdk-go/blob/v2.15.2/protocol/mqtt_paho/v2/message.go), before #1063): `contentType = "Content-Type"` as a User Property (`message.go:21`, `:45`, `:91`; `write_message.go:49`, `:94`, at `v2.15.2`), not the Content Type property. `main` no longer reads that form (probe below).
- **sdk-java**: no MQTT module. The [README](https://github.com/cloudevents/sdk-java/blob/81d64ac49d6e4ecc8c2ff35b4b8e24dfa8ca8336/README.md) feature table has `MQTT Protocol Binding | :x: | :x:`; the [repository tree](https://github.com/cloudevents/sdk-java/tree/81d64ac49d6e4ecc8c2ff35b4b8e24dfa8ca8336) at `81d64ac` (2026-10-13) has `amqp`, `http`, `kafka`, `rocketmq`, `spring`, `sql` and no path containing `mqtt`.

**Against CR-0006's claim.** Both parts are confirmed. The prefix is `ce-`. Binary mode is decided by `ce-specversion` being present, not by the absence of a CloudEvents Content Type. That second part is not an interop divergence: Go tests the Content Type first, as the text does, and the fallback (binary only with the version attribute, an error otherwise) is what `kafka::from_message` and the sketch do too. The text's "otherwise it defaults to binary" would also read a message with no version attribute as binary, and then fail to build an event; the outcomes differ only in the error. What decides interop is the prefix.

## 3. Go works offline

`protocol/mqtt_paho` converts an event to and from a caller-supplied `*paho.Publish` (`WritePubMessage`, `write_message.go:22`; `NewMessage`, `message.go:39`), so no client connects. `interop/mqtt/go/main.go` builds eight events in both modes and writes 16 documents to `test/fixtures/interop/mqtt/go/`; running it twice gives identical files (checked).

```text
cd interop/mqtt/go && go run . ../../../test/fixtures/interop/mqtt/go ../../../test/fixtures/interop/mqtt/cpp      # Go 1.27.1, no network after the module download
```

The document shape (`publish`): `mqtt` ("5.0"), `topic`, `payload_base64`, `content_type` (null when the property is absent), and `user_properties` as an ordered list of `[name, value]` text pairs (sorted by name for a stable file; Go emits extensions in map order). For 3.1.1 the same document would have no properties: the structured goldens' `payload_base64` alone is the 3.1.1 PUBLISH payload.

- Structured documents of an event with extensions are re-sorted by name (`withSortedExtensions`) and checked to be the same JSON document the SDK wrote, so they are decode-equal to the SDK's output, not byte-exact (the Kafka generator makes the same compromise).
- Events: `minimal`, `full`, `extensions`, `text_data`, `binary_data`, `extension_types`, `unicode`, `data_array`; modes `binary` and `structured`.
- The generator has the Go SDK read back each of its 16 documents (all accepted; every attribute, every extension and the data equal), then probes what Go does with the forms the text defines. It exits non-zero if an answer changes:

| form handed to the Go reader | accepted |
|---|---|
| binary, unprefixed User Properties, Content Type property (the text) | no |
| binary, same, plus `datacontenttype` as a User Property (example 3.1.4) | no |
| binary, `ce-` User Properties (Go's own) | yes |
| binary, `CE-specversion` in capitals | no |
| structured, Content Type `application/cloudevents+json; charset=utf-8` (5.0) | yes |
| structured, no properties (3.1.1) | no |
| structured, media type as a User Property named `Content-Type` | no |

Limits: the evidence is the struct, not the MQTT packet bytes (encoding a `Publish` to the wire is paho's job and was not exercised), and no broker ran. A broker that drops or rewrites User Properties is unverified.

Pins: `github.com/cloudevents/sdk-go/protocol/mqtt_paho/v2 v2.0.0-20261020064151-05f0f6812a36`, which resolves `sdk-go/v2 v2.16.2` and `paho.golang v0.23.0` (`interop/mqtt/go/go.mod`). The Kafka interop pins the core at v2.15.2; this one cannot, because the MQTT module at `main` requires the newer core.

## 4. Mapping onto `ce::message`

Read: `include/cloudevents/message.hpp`, `binding/common.hpp`, `binding/kafka.hpp`.

| MQTT 5.0 PUBLISH | `ce::message` | fits as published |
|---|---|---|
| User Properties, an ordered list that may repeat | `header_fields`: `raw_headers` is a `std::vector` of pairs; `add` keeps order and duplicates, `find_exact` is byte for byte | yes |
| Content Type property | the reserved header `content-type`, as in `SWR-KAFKA-0002` | yes, with one adapter duty (below) |
| payload | `body` (`binary`) | yes |
| Topic Name | outside the message: "chosen by the MQTT client" (`:182-183`); no `record` type is needed, unlike Kafka's key | yes |
| 3.1.1 PUBLISH (no properties) | a `message` with empty `header_fields`; or payload entry points as in `nats::to_payload` | yes |
| Payload Format Indicator and other properties | no slot, and the text does not mention them; sdk-go never sets them | a documented gap, not a change |

**The adapter duty.** The Content Type property and a User Property share one namespace once both are in `header_fields`. MQTT allows a User Property literally named `content-type`, and an adapter that copies User Properties into the list cannot tell it from the property. It collides with no attribute (names are `[a-z0-9]+`), so it is not a break, but the adapter must write the Content Type property under that name and a User Property of the same name must be dropped or refused there, not forwarded. That is a documented duty, a requirement and a named test; Kafka has the same shape.

The CR guessed a new optional trait (a reserved-name set) would be needed for the unprefixed rule, because with an empty `attribute_prefix` `carries_prefix` is true for every header. The sketch shows it is not: the binding builds the list of attribute fields first (20 lines: drop `content-type`, drop names no attribute can have, tolerate the 3.1.4 duplicate) and hands that to `binding::read_attributes`. A trait would be a tidier home and would be an addition too. The `"ce-"` rule needs neither.

**Addition, not a break.** The binding is a new header in `ce::v4::mqtt` (like `binding/kafka.hpp`), a new namespace and a new target; `message.hpp`, `common.hpp` and every `v1` to `v3` declaration are untouched, so `scripts/frozen_api_diff.py` passes it (SWR-BUILD-0015) and `docs/SPEC.md` section 3 rule 4 and D-BUILD-4 are met. Behaviour already documented does not change. The one core behaviour the binding inherits is that `read_attributes` refuses two headers carrying one attribute name; for MQTT that is the right reading of a repeated attribute User Property, and the sketch applies it only after the foreign properties are filtered out.

## 5. Prototype

Scratch only, not committed: `.../tmp/spike-mqtt/proto/mqtt_sketch.hpp` (165 lines, comments included) and `roundtrip.cpp` (138). It includes the repository's real headers read-only and adds nothing to them. Contents: `ce::mqtt::spec_rules` and `go_rules` (two traits types, one implementation), `detect_content_mode`, `to_message<Rules, Codec>` (binary and structured), `from_message<Rules, Codec>`, `to_payload_3_1_1`, `from_payload_3_1_1`. Not included: `from_message_moving`, doc comments, the policy parameter of option C.

Result (g++ 16.2 `-std=c++20` and `-std=c++23`, Apple clang `-std=c++20`, all with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`): **226 checks, 0 failures**, covering the 16 Go goldens:

- with `go_rules`, every Go golden decodes, and the event written back is the golden: the same sorted User Properties, and the same payload (bytes in binary mode, the same JSON document in structured mode);
- with `spec_rules`, every Go structured golden decodes and every Go binary golden is refused as `not_a_cloudevent` (the divergence, as a test);
- `spec_rules` binary output has no name starting with `ce-`, decodes back, and is stable on a second round trip;
- the 3.1.1 payload of a structured golden decodes and re-encodes to the same document;
- the 3.1.4 example shape (duplicate `datacontenttype`, a foreign `x-request-id`) is read, and a conflicting `datacontenttype` is refused.

### Cost against the earlier bindings

| | Kafka #24 | NATS #25 | NATS binary #31 | Kafka interop #95 | MQTT estimate |
|---|---|---|---|---|---|
| size | +806 lines, 18 files | +334, 12 | +422, 14 | +2767, 83 (mostly goldens) | slice 1 about +700 over 16 files; slice 2 about +500; interop about +1500, mostly goldens |
| header | 207 lines | about 155 | - | - | 170 to 220 (sketch 165) |
| requirements | 9 SWR and 1 SYS | - | - | SWR-SEC-0008 | 8 to 10 SWR and 1 SYS for both slices |
| tests | `kafka_binding_test.cpp`, 339 lines at first | - | - | `interop_kafka_test.cpp` | 300 to 400 lines |
| interop | - | - | - | Go and Java | Go only: the generator is 295 lines already (this PR); a C++ producer of about 200 lines and a Go verify of about 100 more |
| fuzz | - | - | - | - | one target, about 80 lines (`fuzz_kafka_message.cpp` is 79) plus a corpus |
| docs | decisions, `SPEC.md`, GUIDE row | similar | D-NATS-1 | D-INTEROP-5 | three decisions (name rule, foreign properties, 3.1.1 entry), a GUIDE row and a `SPEC.md` line |
| packaging | CMake target, install list, `cloudevents.cppm` | same | - | - | the same three files; no new dependency |

The estimate is the author's, from the sizes above and the sketch; the earlier columns are `gh pr view` numbers. Size S for slice 1 and M for both holds. The interop PR is smaller than #95 because there is no Java peer, and weaker for the same reason.

## Open decisions and risks

- **The name rule for binary mode** (option A, B or C above). Needed only for slice 2.
- **A foreign User Property.** The text does not say what a receiver does with `x-request-id`, which is not a valid attribute name. The sketch ignores it; the alternative, refusing the message, would make every broker-side annotation fatal. Needs a ruling and a named test.
- **A foreign User Property with a valid attribute name** is read as an extension under the unprefixed rule; that is what the text asks for and also the cost of it.
- **`datacontenttype` in two places** (3.1.4). The sketch writes only the Content Type and reads both; writing both, as the example shows, is a possible choice and no SDK does it.
- **3.1.1 receive** cannot be told from 5.0 without a Content Type: a message with no properties is "not a CloudEvent" on the 5.0 entry point and structured JSON on the 3.1.1 one, so the caller must know the protocol version; a `from_message` that guessed would break the rule on one side.
- **No Payload Format Indicator**: the text is silent, Go sets nothing; `message` has no field for it, so an adapter that wants it sets it.
- **Unverified**: MQTT 5.0 limits not re-read here (a UTF-8 string and so a User Property value is limited to 65,535 bytes; `encode_value` would have to refuse longer, at the OASIS text 1.5.4); the other SDKs (Rust, C#, Python) as peers; a real broker; the MQTT packet encoding; whether Java's absence is permanent.

## Reproduce

```text
cd interop/mqtt/go && go run . ../../../test/fixtures/interop/mqtt/go ../../../test/fixtures/interop/mqtt/cpp      # regenerates the 16 goldens and runs the probes
git diff --exit-code test/fixtures/interop/mqtt/go          # empty: deterministic
```

The C++ sketch is not in the repository; it was built with `g++-16 -std=c++20 -I include -isystem <ctre>/include -isystem <nlohmann> roundtrip.cpp` against this tree.
