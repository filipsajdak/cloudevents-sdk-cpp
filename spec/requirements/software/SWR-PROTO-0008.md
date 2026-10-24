---
uid: SWR-PROTO-0008
title: Data is read from any of the three data fields
type: software
status: reviewed
priority: medium
rationale: >
  The Go SDK writes binary_data where the Java SDK writes text_data for the same event, so a
  reader that wants both SDKs' events has to accept all three fields. The oneof semantic of
  Protobuf (the last field on the wire wins) decides a message that carries two. The empty
  binary_data, JSON classification and proto_data rules are open questions of CR-0009.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The `protobuf_format` decoder shall return the payload of text_data as a `std::string` holding
the bytes unchanged and the payload of binary_data as a `binary`, shall accept proto_data as an
opaque payload, and shall, for a message with more than one data field, use the one that appears
last.
