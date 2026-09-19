---
uid: SWR-CORE-0009
title: Byte-for-byte timestamp round-trip for canonical input
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 requires `to_string` to round-trip canonical input byte for byte so
  that re-emitting a received event does not alter a value other CloudEvents SDKs
  may have signed or compared as text.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/detail/timestamp.hpp]
verified_by: [test:test/core_test.cpp::core-timestamp-roundtrip]
owner: filip.sajdak
version: 1
---
When a `timestamp` was parsed from canonical RFC 3339 text, `to_string` shall
return a string equal byte for byte to that input text.
