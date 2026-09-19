---
uid: SWR-SEC-0003
title: Every error code has a negative test
type: software
status: approved
priority: high
rationale: >
  SPEC 6 requires a negative test per errc value. An error code that is never
  exercised is one whose detection logic has never been shown to fire, which is the
  common way a decode path silently accepts input it should reject.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:test/errors_test.cpp::errc-negative-coverage]
owner: filip.sajdak
version: 1
---
The SDK shall carry at least one test per errc enumerator that presents input causing
that error and asserts the returned code.
