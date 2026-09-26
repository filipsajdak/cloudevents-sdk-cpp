---
uid: SWR-EXT-0008
title: decode_batch_as reads a batch and its typed payloads in one parse
type: software
status: implemented
delivered_in: v0.5.0
priority: medium
rationale: >
  A batch multiplies the cost of a second parse by its length.
  Every event in a batch decoded this way carries the same payload type.
  An event whose payload does not decode as T fails the batch, as a malformed event does today, so a caller never receives a batch with a hole in it.
  The events are those decode_batch would return for the same text and options, including its per-event retention rule (SWR-JSON-0040).
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp, code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/typed_payload_test.cpp::decode-batch-as-reads-a-batch-in-one-parse]
owner: filip.sajdak
version: 2
---
The SDK shall provide `decode_batch_as<T, Codec>(text, decode_options)`, which parses a JSON batch document once and returns, in order, a `decoded<T>` for every event: the event `decode_batch(text, options)` would return, and its payload decoded into `T` from the parsed `data` member.
When any element's payload is absent or does not decode as `T`, the whole batch shall fail with the error `decode_as` reports for that element.
