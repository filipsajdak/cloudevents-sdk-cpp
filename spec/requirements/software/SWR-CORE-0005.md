---
uid: SWR-CORE-0005
title: Core attribute type aliases
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 and CloudEvents v1.0.2 core specification section 3.1 fix the
  attribute type system, so each specified type needs a named alias that the
  format and binding layers can map onto their wire representations.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-attribute-type-aliases]
owner: filip.sajdak
version: 1
---
The core header shall define the attribute type aliases `binary` as
`std::vector<std::byte>`, `uri`, `uri_ref` and `timestamp`.
