---
uid: SWR-CORE-0007
title: Timestamp representation retaining the original UTC offset
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 requires `to_string` to reproduce canonical input byte for byte, which
  is impossible from a UTC instant alone because RFC 3339 permits a non-zero
  offset, so the offset is stored beside the instant.
verification_method: test
security_classification: operational
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/detail/timestamp.hpp]
verified_by: [test:test/core_test.cpp::core-timestamp-representation]
owner: filip.sajdak
version: 1
---
The `timestamp` type shall hold a UTC instant as
`std::chrono::sys_time<std::chrono::nanoseconds>` together with the original
offset of the parsed text as `std::chrono::minutes`.
