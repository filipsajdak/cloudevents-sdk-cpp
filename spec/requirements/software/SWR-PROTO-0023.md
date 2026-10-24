---
uid: SWR-PROTO-0023
title: Protobuf-encoded messages interoperate through the bindings
type: software
status: reviewed
priority: medium
rationale: >
  The Go and Java SDKs carry the Protobuf event as the body of an HTTP request and the value of
  a Kafka record. The goldens of SWR-PROTO-0016 are the bytes of that body, and the message form
  is checked against the SDKs' bindings offline.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0002]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall decode, through each binding, the structured-mode messages recorded from the Go
and Java SDKs with the Protobuf format, and shall produce messages that those SDKs decode to the
event this SDK encoded, with the recordings, generators and verifiers committed together.
