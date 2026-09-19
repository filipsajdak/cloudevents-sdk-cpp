---
uid: SWR-JSON-0020
title: Non-JSON datacontenttype with a string data yields std::string
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 names the one case where a data member is not JSON payload: the producer declared a non-JSON content type and carried the payload as a JSON string.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: []
verified_by: [test:test/json_format_test.cpp::decode-data-string-with-non-json-content-type-yields-string]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads a top-level data member holding a JSON string while the datacontenttype attribute is present and denotes a media type that is not JSON, it shall place the string content into the event as a std::string.
