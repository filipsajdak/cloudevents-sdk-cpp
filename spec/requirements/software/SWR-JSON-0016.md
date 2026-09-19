---
uid: SWR-JSON-0016
title: Binary data emitted under data_base64
type: software
status: approved
priority: high
rationale: >
  The CloudEvents JSON format clause on the data_base64 member carries binary payloads, which JSON cannot represent directly, as SPEC 5.3 restates.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::binary-data-under-data-base64-member]
owner: filip.sajdak
version: 1
---
When the event data holds binary, the json_format encoder shall emit that payload as a JSON string holding its base64 encoding under the top-level data_base64 member.
