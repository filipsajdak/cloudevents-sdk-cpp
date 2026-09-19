---
uid: SWR-JSON-0017
title: std::string data emitted under data as a JSON string
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 distinguishes a textual, non-JSON payload from a json_text payload, and the CloudEvents JSON format clause carries the former as a JSON string under data.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::string-data-under-data-member-as-json-string]
owner: filip.sajdak
version: 1
---
When the event data holds a std::string, the json_format encoder shall emit that payload under the top-level data member as a JSON string.
