---
uid: SYS-CORE-0001
title: Event model for CloudEvents v1.0.2, with validity held by the attribute types
type: system
status: approved
priority: high
rationale: >
  Every format and binding is a projection of one in-memory event model, so the
  attribute type system is the single place where conformance to the core
  specification is decided. CR-0001 moved that decision from a validation
  operation a caller could skip to construction, which no caller can: an event
  holding a forbidden value has no representation, so no code path needs to
  remember to check for one.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-event-model]
owner: filip.sajdak
version: 2
---
The SDK shall provide an event type carrying the CloudEvents v1.0.2 context
attributes as types that refuse, at construction and with a typed error, each
value the core specification forbids.
