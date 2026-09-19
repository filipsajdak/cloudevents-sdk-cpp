---
uid: SWR-CORE-0017
title: validate rejects empty required attributes
type: software
status: approved
priority: high
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 states that a REQUIRED
  attribute must be present and non-empty, and SPEC 5.1 makes `validate()` the
  gate that enforces it on the public aggregate before an event is produced.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-validate-required-non-empty]
owner: filip.sajdak
version: 1
---
When `id`, `source`, `specversion` or `type` is an empty string, `validate()`
shall return a failed result identifying the offending attribute.
