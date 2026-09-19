---
uid: SWR-JSON-0003
title: Codec value constructors for every JSON kind
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 enumerates the constructors the format layer calls when it builds an encoded event, covering every JSON kind the CloudEvents JSON format uses.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_codec_test.cpp::json-codec-value-constructors]
owner: filip.sajdak
version: 1
---
The json_codec concept shall demand constructors that build a codec value from each of null, bool, std::int64_t, double, string, an empty array and an empty object.
