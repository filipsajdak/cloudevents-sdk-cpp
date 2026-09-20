---
uid: SWR-JSON-0033
title: An integer beyond the codec's range never yields a value
type: software
status: approved
priority: high
rationale: >
  nlohmann's get<std::int64_t>() reinterprets an out-of-range unsigned value rather than refusing it, so 18446744073709551615 arrived as -1. That is inside the CloudEvents Integer range, so the format layer's own bounds check did not catch it and the caller was handed a number that was not the one on the wire. The defect was found while writing a second codec whose accessor range-checks.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/codec/nlohmann.hpp]
verified_by: [test:test/json_codec_test.cpp::integer-out-of-range-never-yields-a-value]
owner: filip.sajdak
version: 1
---
When a JSON document carries an integer a codec cannot represent, the codec shall
either reject the document at `parse` or return an error from `as_int` carrying the
out_of_range error code. It shall not return an integer value other than the one
the document carried.
