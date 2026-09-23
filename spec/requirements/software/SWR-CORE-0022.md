---
uid: SWR-CORE-0022
title: constexpr valid_attribute_name validator
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.1 requires the name grammar to be a `constexpr` predicate and SPEC 6
  tests every such validator with `static_assert`, so the rule is decided at
  compile time and shared by validation and the format layers.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/attributes.hpp]
verified_by: [test:test/core_test.cpp::core-valid-attribute-name]
owner: filip.sajdak
version: 1
---
The core header shall provide a `constexpr` predicate `valid_attribute_name`
returning true exactly for names matching the pattern `[a-z0-9]+`.
