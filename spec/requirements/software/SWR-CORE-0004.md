---
uid: SWR-CORE-0004
title: fail helper constructing a failed result
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.1 specifies a `fail(errc, detail)` helper so every failure site builds
  the unexpected value the same way and the construction stays identical between
  the std::expected alias and the polyfill.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/result.hpp]
verified_by: [test:test/core_test.cpp::core-fail-helper]
owner: filip.sajdak
version: 1
---
The core header shall provide a `fail(errc code, detail)` helper returning a
failed `result` whose `error()` carries the given code and detail string.
