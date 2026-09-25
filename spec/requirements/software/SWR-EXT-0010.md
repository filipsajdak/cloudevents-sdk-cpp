---
uid: SWR-EXT-0010
title: encode_as writes a typed payload without an intermediate text form
type: software
status: approved
priority: medium
rationale: >
  A producer that calls set_data and then encode serialises the payload to text, parses it, and serialises the document.
  encode_as builds the payload's DOM and places it under data in the output document directly.
  The event it is given is not modified, so one event can carry several typed payloads to several consumers.
  The payload is JSON, so a datacontenttype that says otherwise would describe the output wrongly; encode_as refuses it rather than rewrite the caller's attribute.
  The owner decided the media type rule on 2026-09-24.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp, code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/typed_payload_test.cpp::encode-as-writes-the-payload-dom]
owner: filip.sajdak
version: 2
---
The SDK shall provide `encode_as<T, Codec>(event, payload)`, which serialises the event's attributes and extensions with the payload of the described type `T` under `data`, placing the payload's DOM in the output document without producing an intermediate text form of it, and without modifying the event.
When the event has no `datacontenttype`, the output shall carry `application/json`; when it has a JSON media type, the output shall keep it; when it has any other media type, `encode_as` shall fail with `type_mismatch`.
