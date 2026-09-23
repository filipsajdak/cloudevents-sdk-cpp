---
uid: SWR-JSON-0031
title: A decoded event always re-encodes
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  SWR-CORE-0020 makes `extension_name` refuse a name that is not [a-z0-9]+, so a decoder that accepted such a name would return an event the encoder refuses. The same document would then decode and fail to re-encode, which a round-trip fuzzer found within a minute. CR-0001 replaced the validation operation this requirement first named with the attribute types, so the property is stated as the round trip it protects.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::decoded-event-always-re-encodes]
owner: filip.sajdak
version: 2
---
When the json_format decoder would return an event that its encoder refuses, it shall instead return an error result. Where the cause is an extension member name that does not match the pattern `[a-z0-9]+`, the error code shall be invalid_attribute_name and the error location shall name the member.
