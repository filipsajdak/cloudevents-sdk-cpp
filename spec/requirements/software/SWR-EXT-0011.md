---
uid: SWR-EXT-0011
title: set_data stores a typed payload as a json_document
type: software
status: reviewed
priority: medium
rationale: >
  set_data builds a DOM from the described value and then serialises it to json_text.
  Storing the DOM instead lets a later encode with the same codec copy it rather than parse it (SWR-JSON-0041).
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When `set_data<T, Codec>` stores a payload of the described type `T`, it shall store it as a json_document built by `Codec`.
