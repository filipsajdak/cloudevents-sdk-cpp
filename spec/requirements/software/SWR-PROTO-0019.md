---
uid: SWR-PROTO-0019
title: http structured mode in the Protobuf format
type: software
status: reviewed
priority: medium
rationale: >
  An HTTP message in structured mode carries the whole event in the body with the content type
  of the format. The owner ruled on 2026-10-25 (CR-0009, items 11 and 12) that the entry points are new names, for example
  to_message_protobuf and from_message_protobuf, and that the published from_message does not detect the format.
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
