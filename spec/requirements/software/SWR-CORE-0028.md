---
uid: SWR-CORE-0028
title: A failure type usable in a constant expression
type: software
status: approved
priority: high
rationale: >
  `error` owns two `std::string` members, so it cannot exist in a constant
  expression, which is why the `constexpr` on `base64_decode` and
  `percent_decode` is honoured only on their success paths. Measured on GCC 16
  and Clang 23, a `std::string` escapes constant evaluation when it is short
  enough for the small-string buffer and not otherwise, so a constexpr `fail`
  would compile as a function of how long its message is. ADR-0008 adds a failure
  type rather than a second result type, because the compile-time path has no
  value-or-error to carry.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/result.hpp]
verified_by:
  - test:test/static_error_test.cpp::static-error-survives-constant-evaluation
  - test:test/static_error_test.cpp::static-error-widens-into-error
owner: filip.sajdak
version: 1
---
The result header shall define a `static_error` carrying an `errc` and two
`std::string_view` members, usable in a constant expression and convertible to
`error` at the run-time boundary.
