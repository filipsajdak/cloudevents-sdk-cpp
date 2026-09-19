---
uid: SWR-BUILD-0005
title: Public API published through inline namespace ce::v1
type: software
status: approved
priority: high
rationale: >
  SPEC 3 rule 4 places the public API in an inline versioned namespace so the
  mangled names carry the API generation and a consumer linking two generations
  gets a link error instead of silent one-definition-rule breakage.
verification_method: test
security_classification: operational
derived_from: [SYS-BUILD-0001]
satisfied_by: []
verified_by: [test:test/build_test.cpp::config-inline-namespace-v1]
owner: filip.sajdak
version: 1
---
The SDK shall declare every public entity inside the inline namespace `ce::v1`.
