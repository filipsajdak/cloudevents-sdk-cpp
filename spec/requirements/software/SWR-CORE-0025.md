---
uid: SWR-CORE-0025
title: Source checked for non-emptiness and encoding only
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  SPEC 5.1 states the leniency principle of being strict on produce and tolerant
  on consume, and exempts `source` from full RFC 3986 validation so events from
  producers using a looser URI-reference form still decode. Well-formed UTF-8 is
  checked because a value that is not encodable cannot be put on any binding's
  wire, which is a different question from URI syntax.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/attributes.hpp]
verified_by: [test:test/core_test.cpp::core-source-non-empty-only]
owner: filip.sajdak
version: 2
---
The `source` attribute type shall accept any non-empty, well-formed UTF-8 value
without applying RFC 3986 syntax rules.
