---
uid: SWR-JSON-0027
title: Constexpr base64 encode and decode per RFC 4648 section 4
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 requires base64 in the header so it is usable at compile time, and RFC 4648 section 4 is the alphabet the CloudEvents JSON format names for data_base64 and Binary attributes.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/base64_test.cpp::base64-rfc4648-section-4-vectors]
owner: filip.sajdak
version: 1
---
The base64 header shall provide encode and decode functions that implement the standard alphabet of RFC 4648 section 4 and that are usable in a constant expression.
