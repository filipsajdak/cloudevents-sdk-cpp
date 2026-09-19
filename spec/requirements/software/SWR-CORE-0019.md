---
uid: SWR-CORE-0019
title: validate rejects present but empty optional string attributes
type: software
status: approved
priority: high
rationale: >
  CloudEvents v1.0.2 core specification section 3.1 forbids an empty string for
  any attribute that is present, so SPEC 5.1 has `validate()` treat a present and
  empty optional attribute as a violation rather than as absence.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-validate-optional-non-empty]
owner: filip.sajdak
version: 1
---
When an optional string attribute is present and holds an empty string,
`validate()` shall return a failed result identifying that attribute.
