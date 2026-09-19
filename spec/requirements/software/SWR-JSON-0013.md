---
uid: SWR-JSON-0013
title: Binary attribute values as base64 strings
type: software
status: approved
priority: high
rationale: >
  The CloudEvents JSON format clause on type system mapping renders the Binary attribute type as a base64 encoded JSON string, as SPEC 5.3 restates.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::binary-attribute-as-base64-string]
owner: filip.sajdak
version: 1
---
The json_format encoder shall emit a Binary attribute value as a JSON string holding its base64 encoding, and the decoder recovers the octets from that string.
