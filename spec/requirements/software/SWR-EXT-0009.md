---
uid: SWR-EXT-0009
title: from_value_as reads a typed event from a document already parsed
type: software
status: approved
priority: medium
rationale: >
  A caller that parsed the document itself, to route on it or to read a transport envelope, already holds the DOM.
  json_format offers from_value for that case, and the typed path needs the same entry point.
  The event is the one from_value returns, and the payload is read from the caller's data member, which stays the caller's (D-JSON-6).
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp, code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/typed_payload_test.cpp::from-value-as-reads-a-parsed-document]
owner: filip.sajdak
version: 2
---
The SDK shall provide `from_value_as<T, Codec>(document)`, which reads, from a JSON document the caller has already parsed with `Codec`, `decoded<T>{.event, .payload}`: the event `from_value(document)` would return, and its payload decoded into the described type `T` from the document's `data` member, with the failures of `decode_as`.
