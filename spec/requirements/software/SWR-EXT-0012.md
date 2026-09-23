---
uid: SWR-EXT-0012
title: data_as reads a matching json_document without parsing
type: software
status: reviewed
priority: high
rationale: >
  data_as parses the payload text on every call.
  A json_document built by the requested codec is already that codec's DOM, so the typed value can be read from it directly.
  A document from another codec is converted as SWR-JSON-0042 states.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
When the event data holds a json_document built by `Codec`, `data_as<T, Codec>` shall read the payload from that document without serialising or parsing it.
