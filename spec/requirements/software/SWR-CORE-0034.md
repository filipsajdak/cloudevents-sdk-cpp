---
uid: SWR-CORE-0034
title: A json_document yields its DOM only to the codec that built it
type: software
status: reviewed
priority: high
rationale: >
  Reading a type-erased document as the wrong codec's value would be undefined behaviour.
  The document records the declared identity of the codec that built it, and hands its DOM out only when the requesting codec declares the same identity.
  A mismatch then selects the slower conversion path, never a bad cast.
  An identity compared by value is immune to linker folding and stable across builds and shared libraries.
  Uniqueness is the codec author's contract, as the one-definition rule is, and the in-tree identities live under `io.cloudevents.cpp.`.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When a codec requests the DOM of a `json_document`, the `json_document` type shall yield it only if the requesting codec's declared identity equals the identity of the codec that built it.
