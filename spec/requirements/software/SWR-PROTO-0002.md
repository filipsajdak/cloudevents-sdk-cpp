---
uid: SWR-PROTO-0002
title: Protobuf media type constants
type: software
status: reviewed
priority: medium
rationale: >
  The Protobuf format clause fixes application/cloudevents+protobuf for transports with content
  identification, as SWR-JSON-0025 does for JSON. The batch media type is a separate capability
  and is an open question in CR-0009.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The `protobuf_format` class shall expose application/cloudevents+protobuf as the media type of
an encoded event.
