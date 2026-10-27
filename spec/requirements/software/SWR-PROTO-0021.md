---
uid: SWR-PROTO-0021
title: nats structured mode in the Protobuf format
type: software
status: reviewed
priority: medium
rationale: >
  As SWR-PROTO-0019, for a NATS message. A NATS payload has no content type of its own, so the
  name of the entry point carries the format (CR-0009, ruling 24 of 2026-10-27): there is no tag
  argument and no overload of a published function.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 2
---
The NATS binding shall provide `nats::to_message_protobuf` and `nats::from_message_protobuf` for
the message form, and `nats::to_payload_protobuf` and `nats::from_payload_protobuf` for the
payload form, whose bytes are those of `protobuf_format::encode`. `nats::from_payload_protobuf`
shall always read its argument as Protobuf, with no argument naming the format, and shall decode
any bytes it is given, returning `errc::parse_error` for bytes that are not an event. The
published `nats::to_payload`, `nats::from_payload`, `nats::to_message` and `nats::from_message`
shall not change.
