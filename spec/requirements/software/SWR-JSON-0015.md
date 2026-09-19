---
uid: SWR-JSON-0015
title: json_text data emitted under data
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 and the CloudEvents JSON format clause on the data member place already-JSON payloads inline under data so that a consumer parses the event once.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::json-text-data-under-data-member]
owner: filip.sajdak
version: 1
---
When the event data holds json_text, the json_format encoder shall emit that payload as the parsed JSON value of the top-level data member.
