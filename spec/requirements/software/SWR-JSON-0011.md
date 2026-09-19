---
uid: SWR-JSON-0011
title: Integer attribute values encoded as JSON numbers
type: software
status: approved
priority: high
rationale: >
  The CloudEvents JSON format clause on type system mapping renders the Integer attribute type as a JSON number, and SPEC 5.3 restates the int32 range that the CloudEvents core type system defines.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::integer-attribute-as-json-number]
owner: filip.sajdak
version: 1
---
The json_format encoder shall emit an Integer attribute value as a JSON number whose value lies within the range of a 32-bit signed integer.
