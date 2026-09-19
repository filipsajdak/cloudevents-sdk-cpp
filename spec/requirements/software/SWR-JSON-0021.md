---
uid: SWR-JSON-0021
title: data and data_base64 together rejected
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 and the CloudEvents JSON format clause forbid carrying a payload twice, and accepting one of the two silently would let a peer show different payloads to different receivers.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::decode-both-data-members-is-error]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads an event object carrying both a data member and a data_base64 member, it shall return an error result.
