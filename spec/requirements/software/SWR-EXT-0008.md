---
uid: SWR-EXT-0008
title: decode_batch_as reads a batch and its typed payloads in one parse
type: software
status: reviewed
priority: medium
rationale: >
  A batch multiplies the cost of a second parse by its length.
  Every event in a batch decoded this way carries the same payload type.
  An event whose payload does not decode as T fails the batch, as a malformed event does today.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall provide `decode_batch_as<T, Codec>`, which parses a JSON batch document once and returns every event together with its payload decoded into the described type `T`.
