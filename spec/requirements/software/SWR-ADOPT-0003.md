---
uid: SWR-ADOPT-0003
title: Library code compiles with exceptions disabled
type: software
status: approved
priority: high
rationale: >
  SPEC 9 decision D4 commits the SDK to supporting -fno-exceptions builds. This is
  also what forbids calling value() on result<T>, because that accessor throws and
  would make the library untranslatable in such a build.
verification_method: test
security_classification: operational
derived_from: [SYS-ADOPT-0001]
satisfied_by: [code:include/cloudevents/detail/expected_polyfill.hpp]
verified_by: [test:test/consumer/CMakeLists.txt::consumer-fno-exceptions]
owner: filip.sajdak
version: 1
---
The SDK shall compile and behave identically when the consuming translation unit is
built with exceptions disabled, and shall not throw from any library code path.
