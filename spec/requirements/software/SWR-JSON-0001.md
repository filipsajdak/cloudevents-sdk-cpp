---
uid: SWR-JSON-0001
title: json_codec concept abstracts a JSON DOM
type: software
status: approved
priority: high
rationale: >
  SPEC 5.3 makes the codec seam an abstraction over a JSON document object model rather than over the CloudEvents event, so that the format layer owns every CloudEvents rule and a consuming project can plug in the JSON library it already ships.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_codec.hpp]
verified_by: [test:test/json_codec_test.cpp::json-codec-concept-dom-surface]
owner: filip.sajdak
version: 1
---
The json_codec concept shall constrain a type to expose a JSON document object model, comprising a nested value type together with operations over that value, without requiring any operation expressed in terms of CloudEvents events or attributes.
