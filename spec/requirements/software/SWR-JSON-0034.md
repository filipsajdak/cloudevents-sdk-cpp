---
uid: SWR-JSON-0034
title: The kind of a JSON number follows its text, not its magnitude
type: software
status: approved
priority: medium
rationale: >
  A codec that reports a large integer as kind::floating makes the format layer diagnose it as a fractional extension value, which SWR-JSON-0024 refuses for a different and wrong reason. Separating the two questions keeps the diagnosis truthful: kind_of answers whether the text was an integer, and as_int answers whether the value fits.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_codec.hpp]
verified_by: [test:test/json_codec_test.cpp::number-kind-follows-the-text-not-the-magnitude]
owner: filip.sajdak
version: 1
---
When a codec reports the kind of a JSON number, it shall report `kind::integer` if
the text carried no fractional part and no exponent, whatever the magnitude of the
value, and `kind::floating` otherwise.
