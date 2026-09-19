---
uid: SWR-JSON-0018
title: data_base64 decodes to binary data
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 and the CloudEvents JSON format clause on data_base64 fix the decode direction for binary payloads arriving from an untrusted peer.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::decode-data-base64-yields-binary]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads a top-level data_base64 member, it shall decode its base64 string content and place the resulting octets into the event as binary data.
