---
uid: SWR-DESC-0011
title: One parity suite runs against both backends
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 makes backend parity the whole point of the seam, and parity claimed in prose
  is untested; running a single suite twice, once per backend, is what turns the claim
  into a build failure when it stops holding.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/describe.hpp]
verified_by: [test:test/describe_parity_test.cpp::describe-backend-parity]
owner: filip.sajdak
version: 1
---
The test build shall compile one shared describe test suite against the C++20 backend
and against the C++26 backend, asserting that both produce the same field names, the
same field order and the same serialized output.
