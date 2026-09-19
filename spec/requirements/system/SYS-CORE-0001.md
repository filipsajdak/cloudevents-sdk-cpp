---
uid: SYS-CORE-0001
title: Event model and validation for CloudEvents v1.0.2
type: system
status: approved
priority: high
rationale: >
  Every format and binding is a projection of one in-memory event model, so the
  attribute type system and its validation rules are the single place where
  conformance to the core specification is decided.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-validate]
owner: filip.sajdak
version: 1
---
The SDK shall provide an event type carrying the CloudEvents v1.0.2 context
attributes, their specified value types, and a validation operation that reports
each violation of the core specification as a typed error.
