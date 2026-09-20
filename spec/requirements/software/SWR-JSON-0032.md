---
uid: SWR-JSON-0032
title: A null attribute value decodes as unset
type: software
status: approved
priority: high
rationale: >
  CloudEvents JSON format section 2.2 states that a null value encountered while decoding an attribute MUST be treated as the equivalent of unset or omitted. The format specification's own structured example carries an "unsetextension": null member for exactly this reason, so a decoder that rejects it fails on the published example.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::null-attribute-decodes-as-unset]
owner: filip.sajdak
version: 1
---
When the json_format decoder reads a member whose JSON value is null and whose name is
not `data`, it shall treat that attribute as absent rather than returning an error.
The `data` member is excluded because the specification makes an explicit null payload
distinct from an absent one.
