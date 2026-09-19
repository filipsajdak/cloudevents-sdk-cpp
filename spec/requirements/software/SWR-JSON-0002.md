---
uid: SWR-JSON-0002
title: Codec parse and dump operations
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 names parse and dump as the codec entry points; parse consumes untrusted text, so it returns a result rather than signalling failure out of band.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_codec_test.cpp::json-codec-parse-dump-roundtrip]
owner: filip.sajdak
version: 1
---
The json_codec concept shall demand a parse operation taking a string view and returning a result holding the codec value type, and a dump operation taking a value and returning its serialised std::string form.
