---
uid: SWR-JSON-0025
title: Structured and batch media types
type: software
status: approved
priority: high
rationale: >
  The CloudEvents JSON format clause on content types fixes the two media types the HTTP binding matches on, as SPEC 5.3 restates.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::json-format-media-types]
owner: filip.sajdak
version: 1
---
The json_format class template shall expose application/cloudevents+json as the media type of a single encoded event and application/cloudevents-batch+json as the media type of an encoded batch.
