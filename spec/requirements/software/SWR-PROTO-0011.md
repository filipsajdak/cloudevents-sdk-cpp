---
uid: SWR-PROTO-0011
title: Encoding is deterministic
type: software
status: reviewed
priority: medium
rationale: >
  Protobuf does not define a map order; the Go SDK writes the map in a random order and the Java
  SDK in an order that is stable per run only. Sorting the entries by name makes the same event
  produce the same bytes, which goldens, caches and signatures need.
verification_method: test
security_classification: operational
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_decode_test.cpp::protobuf-encoding-is-deterministic]
owner: filip.sajdak
version: 1
---
The `protobuf_format` encoder shall write the attributes map in ascending order of attribute
name, so that equal events encode to equal bytes.
