---
uid: SWR-CORE-0034
title: A json_document yields its DOM only to the codec that built it
type: software
status: reviewed
priority: high
rationale: >
  Reading a type-erased document as the wrong codec's value would be undefined behaviour.
  The document records which codec built it, and hands its DOM out only on a match.
  A mismatch then selects the slower conversion path, never a bad cast.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When a codec requests the DOM of a `json_document` that a different codec built, the `json_document` type shall report that no DOM is available to that codec.
