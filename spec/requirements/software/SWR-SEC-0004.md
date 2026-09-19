---
uid: SWR-SEC-0004
title: Conformance fixtures taken verbatim from the specification documents
type: software
status: approved
priority: high
rationale: >
  SPEC 6 requires every example from the core, JSON and HTTP specification documents
  to be stored verbatim with its expected decoded form. Copying the examples by hand
  into paraphrased tests is how a subtle divergence from the specification survives a
  green suite.
verification_method: test
security_classification: operational
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:test/conformance_test.cpp::conformance-spec-examples]
owner: filip.sajdak
version: 1
---
The SDK shall store every example from the CloudEvents core, JSON format and HTTP
binding documents as a fixture reproducing the published bytes, paired with the
decoded form the test asserts.
