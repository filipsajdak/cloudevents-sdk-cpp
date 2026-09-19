---
uid: SWR-JSON-0024
title: Floating-point extension value rejected
type: software
status: approved
priority: high
rationale: >
  SPEC 9 decision D6 rejects floating-point extension values on decode because the CloudEvents type system has no such attribute type, so accepting one would invent a type the rest of the SDK cannot carry.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::floating-point-extension-value-is-type-mismatch]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads an extension member holding a JSON number with a fractional part, it shall return an error result carrying the type_mismatch error code.
