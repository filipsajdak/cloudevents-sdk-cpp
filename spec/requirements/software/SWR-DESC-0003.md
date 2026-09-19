---
uid: SWR-DESC-0003
title: Field count and field names available at compile time
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.2 requires the field count and the wire names to be usable in constant
  expressions so consumers can size arrays, build lookup tables and assert schema
  expectations without running the program.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: [test:test/describe_parity_test.cpp::field-count-and-names-constexpr]
owner: filip.sajdak
version: 1
---
The reflection seam shall expose `ce::field_count<T>` and `ce::field_names<T>()` as
constant expressions usable in a `static_assert` for every type satisfying
`ce::described`.
