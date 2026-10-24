---
uid: SWR-PROTO-0021
title: nats structured mode in the Protobuf format
type: software
status: reviewed
priority: medium
rationale: >
  As SWR-PROTO-0019, for a NATS message. A NATS payload has no content type of its own; the
  binding of the JSON format already decides how structured mode is told from binary mode, and
  CR-0009 asks the owner for the Protobuf rule.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The NATS binding shall provide an entry point that maps an event to a message whose body is the
bytes of `protobuf_format::encode` and which names application/cloudevents+protobuf as its
content type, and one that maps such a message to an event.
