---
uid: SWR-CORE-0030
title: A timestamp cannot carry more fractional digits than it can express
type: software
status: approved
priority: high
rationale: >
  `fractional_digits` was a public member of an aggregate holding a
  `std::uint8_t`, so 10 through 255 were all reachable by hand while a
  nanosecond instant expresses at most nine. Rendering one divided a place value
  that had already reached zero. The renderer no longer divides, and this closes
  the other half by making the state unreachable rather than survivable.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/detail/timestamp.hpp]
verified_by: [test:test/timestamp_test.cpp::timestamp-refuses-more-digits-than-it-can-express]
owner: filip.sajdak
version: 1
---
When a fractional digit count above nine is offered for a `timestamp`, the
factory constructing it shall return a failed result naming the digit count.
