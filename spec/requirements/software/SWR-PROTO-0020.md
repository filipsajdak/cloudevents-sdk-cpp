---
uid: SWR-PROTO-0020
title: kafka structured mode in the Protobuf format
type: software
status: reviewed
priority: medium
rationale: >
  As SWR-PROTO-0019, for a Kafka record, whose content-type header carries the media type.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0002]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The Kafka binding shall provide an entry point that maps an event to a record whose content-type
header is application/cloudevents+protobuf and whose value is the bytes of
`protobuf_format::encode`, and one that maps such a record to an event.
