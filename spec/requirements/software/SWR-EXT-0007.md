---
uid: SWR-EXT-0007
title: decode_as reads an event and its typed payload in one parse
type: software
status: reviewed
priority: high
rationale: >
  A consumer that wants a typed payload decodes the event and then calls data_as.
  Measured on a four-field payload, that costs 20 to 26 percent more than one parse (CR-0003).
  decode_as returns decoded<T>, an aggregate naming the event and the payload, so neither is read twice.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall provide `decode_as<T, Codec>`, which parses a JSON event document once and returns the event together with its payload decoded into the described type `T`.
