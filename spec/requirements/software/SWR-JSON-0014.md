---
uid: SWR-JSON-0014
title: URI, URI-reference and Timestamp attributes as strings
type: software
status: approved
priority: high
rationale: >
  The CloudEvents JSON format clause on type system mapping renders URI, URI-reference and Timestamp attribute values as JSON strings, as SPEC 5.3 restates.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::uri-uriref-timestamp-as-json-string]
owner: filip.sajdak
version: 1
---
The json_format encoder shall emit a URI, URI-reference or Timestamp attribute value as a JSON string carrying its CloudEvents string form.
