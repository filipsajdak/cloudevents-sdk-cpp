---
uid: SWR-JSON-0010
title: json_format encode and decode entry points
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 fixes the four json_format operations that the HTTP structured and batched content modes build on.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::json-format-entry-points]
owner: filip.sajdak
version: 1
---
The json_format class template parameterised on a json_codec shall provide encode taking an event, decode taking a string view, encode_batch taking a span of events and decode_batch taking a string view, with each decoding operation returning a result.
