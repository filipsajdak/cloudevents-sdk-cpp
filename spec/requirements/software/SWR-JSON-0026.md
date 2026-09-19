---
uid: SWR-JSON-0026
title: Empty batch is valid
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.3 and the CloudEvents JSON batch format clause accept an empty JSON array, so a peer delivering nothing is not a protocol error.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::empty-batch-decodes-to-no-events]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads the batch document consisting of an empty JSON array, it shall return a successful result holding no events.
