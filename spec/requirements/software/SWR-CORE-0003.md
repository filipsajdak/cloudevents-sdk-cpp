---
uid: SWR-CORE-0003
title: result polyfill surface on the C++20 floor
type: software
status: approved
priority: high
rationale: >
  SPEC 3 names C++20 as the floor with full functionality, so SPEC 5.1 requires a
  polyfill exposing the same observers as `std::expected` and covering the
  `result<void>` case used by operations that return no value.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/detail/expected_polyfill.hpp]
verified_by: [test:test/core_test.cpp::core-result-polyfill]
owner: filip.sajdak
version: 1
---
When the standard library does not provide `std::expected`, `result<T>` shall be
a polyfill exposing `has_value`, `operator bool`, `operator*`, `operator->` and
`error()`, together with a `result<void>` specialisation.
