---
uid: SWR-JSON-0019
title: data decodes to json_text by default
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 makes json_text the decode default for the data member, which preserves the received JSON without committing the SDK to interpreting it.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::decode-data-yields-json-text]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads a top-level data member and no rule selects another payload type, it shall place the member value into the event as json_text.
