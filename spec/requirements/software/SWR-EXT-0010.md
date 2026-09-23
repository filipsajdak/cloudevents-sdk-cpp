---
uid: SWR-EXT-0010
title: encode_as writes a typed payload without an intermediate text form
type: software
status: reviewed
priority: medium
rationale: >
  A producer that calls set_data and then encode serialises the payload to text, parses it, and serialises the document.
  encode_as builds the payload's DOM and places it in the output document directly.
  The event it is given is not modified.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall provide `encode_as<T, Codec>`, which serialises an event with a payload of the described type `T` into a JSON event document without producing an intermediate text form of the payload.
