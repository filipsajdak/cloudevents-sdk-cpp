---
uid: SWR-BUILD-0004
title: Polyfills mirror the standard surface so deletion is a no-op
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 3 requires each polyfill to match the standard surface exactly, so
  when the floor rises the polyfill can be removed and user code that compiled
  against it keeps compiling against the standard type.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: [code:include/cloudevents/detail/expected_polyfill.hpp]
verified_by: [test:test/build_test.cpp::config-polyfill-parity]
owner: filip.sajdak
version: 1
---
Where the SDK ships a polyfill for a standard library facility, that polyfill
shall expose the same member and free-function surface as the facility it
stands in for.
