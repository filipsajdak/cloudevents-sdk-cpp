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
  The event is the one decode would return for the same text and options, so the retention limit of SWR-JSON-0040 and the input's own text of SWR-JSON-0043 apply unchanged.
  The payload is read from the data member the parse produced, so it is never serialised and parsed again.
  The failures are those data_as reports, so a caller moving from decode and data_as sees the same errors.
  The owner decided the shape on 2026-09-24.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 2
---
The SDK shall provide `decode_as<T, Codec>(text, decode_options)`, which parses a JSON event document once and returns `decoded<T>{.event, .payload}`: the event `decode(text, options)` would return, and its payload decoded into the described type `T` from the parsed `data` member without serialising or parsing it again.
When the event has no payload, `decode_as` shall fail with `missing_required_attribute`; when its payload is `data_base64` or a string under a non-JSON `datacontenttype`, it shall fail with `type_mismatch`.
