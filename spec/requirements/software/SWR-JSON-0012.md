---
uid: SWR-JSON-0012
title: Out-of-range or fractional Integer attribute rejected
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 makes an Integer attribute outside int32 or carrying a fraction an error, because silently truncating attacker-supplied JSON numbers would change the decoded event.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::integer-attribute-out-of-range-or-fractional-is-error]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads an Integer attribute whose JSON number falls outside the range of a 32-bit signed integer or carries a fractional part, it shall return an error result rather than a truncated value.
