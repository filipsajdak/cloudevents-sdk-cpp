---
uid: SWR-CORE-0006
title: attribute_value variant over the specified attribute types
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 defines `attribute_value` as the closed set of value types permitted
  by CloudEvents v1.0.2 core specification section 3.1, so a variant over exactly
  those alternatives makes an out-of-system type unrepresentable.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: [test:test/core_test.cpp::core-attribute-value-variant]
owner: filip.sajdak
version: 1
---
The core header shall define `attribute_value` as a variant whose alternatives
are exactly `bool`, `int32_t`, `std::string`, `binary`, `uri`, `uri_ref` and
`timestamp`.
