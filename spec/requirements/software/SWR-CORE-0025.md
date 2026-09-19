---
uid: SWR-CORE-0025
title: Source checked for non-emptiness only
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 states the leniency principle of being strict on produce and tolerant
  on consume, and exempts `source` from full RFC 3986 validation so events from
  producers using a looser URI-reference form still decode.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-source-non-empty-only]
owner: filip.sajdak
version: 1
---
The `validate()` operation shall check `source` for non-emptiness alone and
accept any non-empty value without applying RFC 3986 syntax rules.
