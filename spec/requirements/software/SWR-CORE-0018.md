---
uid: SWR-CORE-0018
title: validate accepts only specversion 1.0
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 pins `specversion` to the value 1.0 and open decision D5 declines
  support for 0.3 on receive, so any other version string is a failure rather
  than a silently processed event.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-validate-specversion]
owner: filip.sajdak
version: 1
---
When `specversion` holds a value other than the string 1.0, `validate()` shall
return a failed result reporting the unsupported version.
