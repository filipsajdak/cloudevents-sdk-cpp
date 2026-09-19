---
uid: SWR-CORE-0002
title: result alias of std::expected when the standard library provides it
type: software
status: approved
priority: high
rationale: >
  SPEC 3 lists C++23 as a supported standard where `result<T>` becomes
  `std::expected` with no API difference, so the alias must resolve to the
  standard type whenever the toolchain offers it and the polyfill can later be
  deleted without touching call sites.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/result.hpp, code:include/cloudevents/detail/config.hpp]
verified_by: [test:test/core_test.cpp::core-result-expected-alias]
owner: filip.sajdak
version: 1
---
When the standard library provides `std::expected`, `result<T>` shall be an
alias of `std::expected<T, error>`.
