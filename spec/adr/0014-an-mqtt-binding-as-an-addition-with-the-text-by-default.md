# ADR-0014: An MQTT binding as an addition, with the specification's names by default and the Go prefix as an opt-in

## Status

Accepted 2026-10-24 for the design, on CR-0008, by the owner's rulings of that day (CR-0008, "Owner decisions"): MQTT is a bet, it ships as an addition in `ce::v4`, binary mode follows the specification text by default with an opt-in for sdk-go's `ce-` prefix, and the work comes in two slices.
Two questions stay open and are the owner's (CR-0008, "Open questions"): the treatment of a foreign User Property, and a User Property named `content-type`.
Neither changes the design below; each changes one requirement.
It does not change ADR-0012 or ADR-0013: `ce::v4` and its predecessors stay frozen, and this decision adds to v4 only.

## Context

`docs/SPEC.md` section 1 lists the MQTT binding as out of scope and says design must not block it.
CR-0006 ranked the candidates and recommended MQTT as the cheapest, because it needs no dependency and no new parser.
The spike (`docs/spikes/mqtt.md`, merged in pull request 112) then read the v1.0.2 binding text in full, read sdk-go `protocol/mqtt_paho/v2` at `05f0f68`, ran sdk-go offline to write 16 goldens, and built a 165-line sketch against the repository's headers.
Four of its findings decide the shape.

**The model fits as published.**
An MQTT 5.0 PUBLISH is a payload, a Content Type property and an ordered list of User Properties that may repeat.
`message::header_fields` is an ordered list that keeps duplicates, the Content Type is the reserved header `content-type` as in `SWR-KAFKA-0002`, and the Topic Name is the client's (binding text `:182-183`).
No member of `message` and no declaration of `binding/common.hpp` changes.

**The SDKs and the text disagree in one place.**
Binding text `:168-169`: attribute names "MUST be used unchanged in each mapped User Property"; the example shows `specversion`, `type`, `id`.
sdk-go writes `"ce-" + name` (`write_message.go:97`) and reads only keys with that prefix (`message.go:20`, `:82-89`).
Structured mode, the Content Type property (`message.go:95-97`, `write_message.go:92`), the order of the mode test (`message.go:43`) and the canonical string values agree.
sdk-java has no MQTT binding, so sdk-go is the only peer.

**MQTT 3.1.1 has no properties.**
The text says binary mode "only applies to MQTT 5.0" (`:55-56`) and that 3.1.1 is always structured JSON (`:135-136`).
A message with no properties is "not a CloudEvent" on 5.0 and structured JSON on 3.1.1, so the protocol version cannot be read from the message.
sdk-go does not support 3.1.1 at all (paho.golang is 5.0 only; sdk-go#1190).

**The unprefixed rule does not need a core trait.**
With an empty `attribute_prefix`, `carries_prefix` is true for every header, which the CR-0006 text expected to need a reserved-name set in the core.
The sketch shows it does not: the binding builds the list of attribute fields first (about 20 lines: drop `content-type`, drop names no attribute can have, tolerate the duplicate `datacontenttype` of example 3.1.4) and hands that list to `binding::read_attributes`.

## Decision

**A new header `include/cloudevents/binding/mqtt.hpp` in `ce::inline v4::mqtt`, a new target `ce::binding_mqtt`, no dependency.**
It is an addition in the sense of `docs/SPEC.md` section 3 rule 4 and `SWR-BUILD-0006`: a new namespace and a new target, with no published declaration and no documented behaviour changed.
The frozen-API diff (`SWR-BUILD-0015`) is the check.

**Two slices.**
Slice 1 is structured mode: `to_message` and `from_message` for MQTT 5.0 (Content Type `application/cloudevents+json`, no User Property written, any ignored on read), and `to_payload` and `from_payload` for 3.1.1 (the JSON bytes, no headers).
The text and sdk-go agree here and the Go goldens verify both directions, so slice 1 needs no ruling on the prefix.
Slice 2 is binary mode for 5.0 with the name rule below.

**The protocol version is chosen by the entry point, never guessed.**
`from_message` is MQTT 5.0; the payload entry points are 3.1.1, as `nats::from_payload` is the payload form of NATS.
A `from_message` that guessed would break the rule on one side.

**Binary mode follows the text by default: names unchanged, no prefix.**
This is the reading of a MUST and the owner's ruling of 2026-10-24.
Precedent: D-NATS-1, where the text beat sdk-go.

**The `ce-` prefix is an opt-in name policy, a defaulted trailing template parameter.**
`template <json::json_codec Codec, name_policy Names = unprefixed_names>` on the binary-capable entry points, with two policy types `unprefixed_names` (the default) and `prefixed_names` (`ce-`, read and write).
This is the shape `ce::http::literal_values` has (D-HTTP-1): a named opt-in, a conformant default, the departure visible at the call site.
It is an addition because the entry points are new, and a function with a defaulted parameter cannot be bound to by existing code.
The policy is compile-time because the prefix already is: `binding_traits::attribute_prefix` is a `static constexpr` member, so the policy supplies the traits and the binding stays a single implementation for both names, as the sketch's `spec_rules` and `go_rules` were.

**Foreign User Properties and a User Property named `content-type` are not decided here.**
The requirements carry a proposal each (`SWR-MQTT-0013`, `SWR-MQTT-0008`), marked as such, and the ADR does not depend on either.

## Alternatives considered and declined

| alternative | why declined |
|---|---|
| Follow sdk-go (`ce-` always) | Breaks a MUST of binding text `:168-169`; makes this SDK read and write a form the text does not define; it would be the Go behaviour copied without normative backing, the cost D-NATS-1 refused. |
| The text only, no opt-in | A conformant SDK that cannot read or be read by the one peer there is, for the whole of binary mode. Interop (`STK-INTEROP-0001`) would have no external measure of binary mode at all. |
| An options aggregate (`mqtt::options{.prefix = ...}`) | The prefix is a compile-time member of the traits; a runtime value would turn the traits into state or add a second traits shape to the core, and a call that passes `{}` hides the departure that a type name shows. HTTP chose a policy type for the same reason. An aggregate is still an addition, so this stays reopenable if a second option appears. |
| A reserved-name trait in `binding/common.hpp` | Tidier than the 20-line filter, but it grows the core's concept for one binding. It would be an addition too, so it can follow as a refactor when a second unprefixed binding needs it. |
| Guess the protocol version in `from_message` | A message with no properties would be wrong on one side. |
| Write `datacontenttype` as a User Property as well as the Content Type, as example 3.1.4 shows | No SDK does it, `:62-66` and `:146-147` do not call for it, and a receiver has no rule for which copy wins. The binding reads the duplicate when equal and writes one. |
| Structured mode only, forever | The text defines binary mode and the rest of the protocol maps onto the model with no change; the cost is slice 2 of about 500 lines. |
| Refuse a message with a foreign User Property | Open question 1. The spike's argument against it: broker-side annotations would be fatal. Left to the owner. |

## Consequences

### Positive

- MQTT joins HTTP, Kafka and NATS as a binding with the same shape, no dependency and no change to a frozen API.
- Slice 1 ships with external evidence (16 Go goldens) and no open ruling.
- A Go publisher or subscriber is reachable by a visible opt-in, and the default is the text.

### Negative

- Binary mode has one peer and no second opinion: sdk-java has no MQTT binding, and the spike did not run a broker or encode the MQTT packet.
- Two behaviours of one binary mode double the cases to test; the interop requirement (`SWR-MQTT-0015`) pins both.
- Under the unprefixed default a foreign User Property with a valid attribute name is read as an extension, which is what the text asks for and the cost of it.

### Neutral

- `from_message_moving` for MQTT is not decided here (CR-0008, open question 3).
- The Payload Format Indicator, QoS and retain are outside `message`, as the text is silent on them.

## References

- CR-0008 (this decision's request), CR-0006 (the ranking), `docs/spikes/mqtt.md`
- ADR-0007 (the binding core), ADR-0012 and ADR-0013 (frozen generations, additions)
- D-HTTP-1 (the opt-in precedent), D-NATS-1 (the text over sdk-go), D-BIND-3 (an addition the frozen-API gate accepts)
- `SYS-MQTT-0001`, `SWR-MQTT-0001` to `SWR-MQTT-0019`
- CloudEvents v1.0.2, `bindings/mqtt-protocol-binding.md`; sdk-go `protocol/mqtt_paho/v2` at `05f0f6812a369b6950568b0b28788cfb44cc3279`
