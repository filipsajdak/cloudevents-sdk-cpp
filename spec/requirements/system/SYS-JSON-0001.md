---
uid: SYS-JSON-0001
title: JSON event format, structured and batch
type: system
status: approved
priority: high
rationale: >
  The JSON format is the interchange representation every other CloudEvents SDK
  implements, so it is the format against which interoperability is measured, and
  it is a prerequisite for the structured and batched HTTP content modes.
verification_method: test
security_classification: security-relevant
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::json-format-entry-points]
owner: filip.sajdak
version: 1
---
The SDK shall encode and decode CloudEvents in the JSON event format, in both the
single-event and the batch representation, through a JSON library the consuming
project selects.
