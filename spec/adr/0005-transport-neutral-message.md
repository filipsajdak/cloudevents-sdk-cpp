# ADR-0005: Bindings map to a transport-neutral message, never to a transport

## Status

Accepted 2026-09-19.

## Context

`docs/SPEC.md` §1 places every real transport adapter out of scope for v0.1 and
`CLAUDE.md` forbids the library from performing network input or output. The HTTP
binding still has to express header and body mappings that are meaningful to an
HTTP library the SDK does not choose.

## Context is also forward-looking

Kafka, MQTT, AMQP, NATS and WebSocket bindings are out of scope for v0.1 but the
design must not block them. A binding coupled to an HTTP type would have to be
rewritten for each.

## Decision

Bindings map events to and from `ce::message`, an aggregate of an ordered,
case-insensitively-searchable header multimap and a `binary` body. No HTTP library
type appears anywhere in the SDK. `http::to_message` and `http::from_message`, plus
batched variants, are pure functions over that type.

The consuming application is responsible for moving a `message` onto its own HTTP
client or server. The SDK owns the envelope; it never owns the wire.

## Consequences

### Positive
- The SDK adds no transport dependency, which is a precondition for adoption in a
  service that has already chosen one.
- The binding is testable entirely in-process, with no sockets and no fixtures that
  depend on a server being up.
- A future Kafka or NATS binding reuses the same target shape.

### Negative
- Callers write a small adapter from `ce::message` to their HTTP library. This is
  a handful of lines but it is not zero.

### Neutral
- Header ordering and duplicates are preserved, because the HTTP binding
  specification permits repeated headers and the batched mode depends on
  Content-Type inspection.

## References

- `docs/SPEC.md` §1, §5.4
- SWR-HTTP-0001 through SWR-HTTP-0014
