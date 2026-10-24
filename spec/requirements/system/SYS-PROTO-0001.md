---
uid: SYS-PROTO-0001
title: Protobuf event format, single event
type: system
status: reviewed
priority: high
rationale: >
  The Go and Java SDKs ship the CloudEvents Protobuf format, so an event written by either onto
  a Protobuf-encoded stream has to be readable here, and the reverse. STK-INTEROP-0001 makes
  agreement with the established SDKs the external correctness measure. The format parses
  untrusted bytes, so it is a security-relevant surface. CR-0009 records the owner's decision to
  go ahead (2026-10-24), the hand-written codec and the field numbers.
verification_method: test
security_classification: security-relevant
derived_from: [STK-INTEROP-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall encode an event to, and decode an event from, the CloudEvents Protobuf event
format of specification v1.0.2 as a sequence of bytes, with no third-party library, so that an
event written by the Go or Java SDK is read back as the event it encoded and an event written
here is read back by those SDKs.
