---
uid: SWR-CORE-0023
title: constexpr reserved_name validator
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.1 requires a `constexpr` predicate that recognises the context attribute
  names defined by CloudEvents v1.0.2 core specification section 3.1, so an
  extension cannot shadow a specified attribute.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-reserved-name]
owner: filip.sajdak
version: 1
---
The core header shall provide a `constexpr` predicate `reserved_name` returning
true exactly for the context attribute names defined by CloudEvents v1.0.2.
