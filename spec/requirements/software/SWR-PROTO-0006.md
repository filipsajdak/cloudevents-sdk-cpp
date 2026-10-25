---
uid: SWR-PROTO-0006
title: A timestamp is carried as an instant
type: software
status: reviewed
priority: medium
rationale: >
  google.protobuf.Timestamp holds seconds and nanoseconds from the epoch and no offset. The
  timestamp type of this SDK also keeps the offset and the digits of the fraction (SWR-
  CORE-0007), which the wire cannot carry, so a decoded time is UTC. Its range is the one of the
  timestamp type, roughly the years 1678 to 2262, and a Timestamp outside it, or with nanos
  outside 0 to 999999999, is refused as the JSON decoder refuses the same instant. Ruled by the owner on 2026-10-25 (CR-0009, item 6): the decoded time carries the fewest
  digits that hold the nanoseconds.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001]
satisfied_by: [code:include/cloudevents/format/protobuf_format.hpp]
verified_by: [test:test/protobuf_format_test.cpp::protobuf-time-is-an-instant, test:test/protobuf_format_test.cpp::protobuf-time-out-of-range-is-refused, test:test/protobuf_format_test.cpp::protobuf-time-negative-nanos-is-refused]
owner: filip.sajdak
version: 2
---
The `protobuf_format` encoder shall write a timestamp as seconds and nanoseconds of its UTC
instant, and the decoder shall return a timestamp in UTC with 0, 3, 6 or 9 fraction digits, the fewest that hold the nanoseconds, and shall fail with
`errc::invalid_timestamp` for a Timestamp whose nanos lie outside 0 to 999999999 or whose
instant the timestamp type cannot hold.
