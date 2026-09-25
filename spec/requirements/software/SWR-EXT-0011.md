---
uid: SWR-EXT-0011
title: set_data stores a typed payload as a json_document
type: software
status: reviewed
priority: medium
rationale: >
  set_data built a DOM from the described value and then serialised it to json_text.
  Storing the DOM instead lets a later encode with the same codec copy it rather than parse it (SWR-JSON-0041), and a later data_as with the same codec read it without parsing (SWR-EXT-0012).
  A json_text payload never equals a json_document payload (D-JSON-4), so an event written by set_data compares equal with the same event holding the payload as a document, not as text.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 2
---
When `set_data<T, Codec>` stores a payload of the described type `T`, it shall store it as a json_document built by `Codec` and set `datacontenttype` to `application/json`.
