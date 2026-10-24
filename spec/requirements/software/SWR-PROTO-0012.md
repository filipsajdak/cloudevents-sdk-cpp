---
uid: SWR-PROTO-0012
title: A decoded event always re-encodes
type: software
status: reviewed
priority: medium
rationale: >
  The counterpart of SWR-JSON-0031: whatever the decoder accepts, the encoder can write, and
  decoding that output gives an equal event. The fuzz target asserts it.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When the `protobuf_format` decoder accepts a byte sequence, the encoder shall encode the
resulting event, and decoding that output shall return an event equal to the first.
