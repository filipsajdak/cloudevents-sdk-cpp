---
uid: SWR-EXT-0009
title: from_value_as reads a typed event from a document already parsed
type: software
status: reviewed
priority: medium
rationale: >
  A caller that parsed the document itself, to route on it or to read a transport envelope, already holds the DOM.
  json_format offers from_value for that case, and the typed path needs the same entry point.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall provide `from_value_as<T, Codec>`, which reads an event and its payload of the described type `T` from a JSON document the caller has already parsed with `Codec`.
