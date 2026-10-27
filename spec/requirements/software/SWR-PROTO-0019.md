---
uid: SWR-PROTO-0019
title: http structured mode in the Protobuf format
type: software
status: reviewed
priority: medium
rationale: >
  An HTTP message in structured mode carries the whole event in the body with the content type
  of the format. The owner ruled on 2026-10-25 (CR-0009, items 11 and 12) that the entry points are new names
  and that the published from_message does not detect the format, and on 2026-10-27 (items 23 and 25) the exact
  names and what a message of another content type returns.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 2
---
The HTTP binding shall provide `http::to_message_protobuf`, which maps an event to a message whose
content type is application/cloudevents+protobuf and whose body is the bytes of
`protobuf_format::encode`, and `http::from_message_protobuf`, which maps such a message to an event.
`http::from_message_protobuf` shall return `errc::not_a_cloudevent` for a message whose content
type is not application/cloudevents+protobuf. The published `http::to_message` and
`http::from_message` shall not change.
