---
uid: SWR-JSON-0004
title: Codec object and array mutation
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 lists set and push as the mutation surface the encoder uses to assemble an event object and a batch array.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_codec_test.cpp::json-codec-set-and-push]
owner: filip.sajdak
version: 1
---
The json_codec concept shall demand a set operation that stores a named member into an object value and a push operation that appends an element to an array value.
