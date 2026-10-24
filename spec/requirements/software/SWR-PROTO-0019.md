---
uid: SWR-PROTO-0019
title: http structured mode in the Protobuf format
type: software
status: reviewed
priority: medium
rationale: >
  An HTTP message in structured mode carries the whole event in the body with the content type
  of the format. The entry-point names are an open question of CR-0009.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The HTTP binding shall provide an entry point that maps an event to a message whose content type
is application/cloudevents+protobuf and whose body is the bytes of `protobuf_format::encode`,
and one that maps such a message to an event.
