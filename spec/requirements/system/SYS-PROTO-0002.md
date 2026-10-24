---
uid: SYS-PROTO-0002
title: Protobuf structured mode in the protocol bindings
type: system
status: reviewed
priority: medium
rationale: >
  The Protobuf format is carried by a transport as the body of a message with the media type
  application/cloudevents+protobuf. The bindings map to and from a transport-neutral message
  (ADR-0005), so the Protobuf format needs no transport type. This is the second slice of
  CR-0009 and follows the format-only slice.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall map an event to and from the HTTP, Kafka and NATS message in structured mode using
the Protobuf event format, naming no transport type, so that a Protobuf-encoded event written by
another SDK onto those transports is read back unchanged.
