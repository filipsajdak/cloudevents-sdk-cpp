---
uid: SWR-JSON-0041
title: A json_document from the encoding codec is copied, not re-parsed
type: software
status: approved
priority: high
rationale: >
  Encoding a json_text payload parses it before splicing it under data.
  A json_document built by the same codec is already a DOM of the right type.
  Copying it removes the parse, and the serialisation that produced the text before it.
verification_method: test
security_classification: operational
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::encode-copies-same-codec-document]
owner: filip.sajdak
version: 1
---
When the event data holds a json_document built by the encoding codec, the json_format encoder shall place a copy of that document under the data member without serialising or parsing it.
