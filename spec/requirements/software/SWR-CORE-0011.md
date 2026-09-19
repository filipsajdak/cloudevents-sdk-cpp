---
uid: SWR-CORE-0011
title: Prohibition on std::chrono::parse in timestamp handling
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.1 forbids `std::chrono::parse` because its availability and locale
  behaviour differ across the toolchains named in SPEC 8, which would make the
  accepted timestamp grammar depend on the standard library in use.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/detail/timestamp.hpp]
verified_by: [test:test/core_test.cpp::core-timestamp-no-chrono-parse]
owner: filip.sajdak
version: 1
---
The core header shall not use `std::chrono::parse` in any timestamp parsing or
formatting path.
