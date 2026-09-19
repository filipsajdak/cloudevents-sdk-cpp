---
uid: SWR-DESC-0001
title: The described concept identifies types carrying field metadata
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 makes the reflection seam a customization point, so every consumer needs one
  compile-time predicate to constrain its templates on rather than testing for a macro
  or a backend feature test.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/describe.hpp]
verified_by: [test:test/describe_parity_test.cpp::described-concept]
owner: filip.sajdak
version: 1
---
The reflection seam header shall define a concept `ce::described<T>` that is satisfied
exactly by those types for which the active backend can enumerate fields.
