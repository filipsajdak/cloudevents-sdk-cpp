---
uid: SWR-CORE-0017
title: A required attribute refuses empty text at construction
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 states that a REQUIRED
  attribute must be present and non-empty. CR-0001 found that enforcing this in
  `validate()` left the rule optional in practice, because nothing obliged a
  caller to call it. ADR-0008 moves the check to the only place the value can
  enter the program.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/attributes.hpp]
verified_by: [test:test/core_test.cpp::core-required-attributes-refuse-empty]
owner: filip.sajdak
version: 2
---
When the text offered for an `id`, `source` or `type` attribute is empty, the
factory constructing that attribute shall return a failed result identifying the
attribute.
