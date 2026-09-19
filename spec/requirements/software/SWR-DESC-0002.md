---
uid: SWR-DESC-0002
title: Field enumeration in declaration order over const and non-const objects
type: software
status: approved
priority: high
rationale: >
  SPEC 5.2 fixes the callback shape `f(std::string_view, member&)` and declaration order
  so that JSON member order, extension mapping and header mapping are reproducible, and
  so encoding a const object and decoding into a mutable one use the same traversal.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: [test:test/describe_parity_test.cpp::for-each-field-order-and-constness]
owner: filip.sajdak
version: 1
---
The reflection seam shall provide `ce::for_each_field(obj, f)` that invokes
`f(std::string_view name, member&)` once per described field in declaration order, for
both a const and a non-const `obj`.
