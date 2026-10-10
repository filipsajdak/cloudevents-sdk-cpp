---
uid: SWR-EXT-0011
title: set_data stores a typed payload as a json_document
type: software
status: approved
priority: medium
rationale: >
  set_data built a DOM from the described value and then serialised it to json_text.
  Storing the DOM instead lets a later encode with the same codec copy it rather than parse it (SWR-JSON-0041), and a later data_as with the same codec read it without parsing (SWR-EXT-0012).
  In ce::v3 it also set datacontenttype to application/json whatever the event declared; ce::v4 sets it only where none is declared and keeps a media type is_json_content_type accepts (SWR-CORE-0024), as encode_as does (SWR-EXT-0010), and SWR-EXT-0013 states the refusal of any other.
  Because the write can be refused, set_data and event_of::set_data return result<void> and event_of::with_data returns result<event_of>; the owner decided that on 2026-09-28 and confirmed the return types on 2026-09-29 (CR-0004).
  A json_text payload never equals a json_document payload (D-JSON-4), so an event written by set_data compares equal with the same event holding the payload as a document, not as text.
verification_method: test
security_classification: operational
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp]
verified_by: [test:test/typed_payload_test.cpp::set-data-stores-a-document, test:test/typed_payload_test.cpp::set-data-refuses-a-non-json-media-type]
owner: filip.sajdak
version: 3
---
When `set_data<T, Codec>` stores a payload of the described type `T`, it shall store it as a json_document built by `Codec`, set `datacontenttype` to `application/json` if the event declares none, and keep a `datacontenttype` that `is_json_content_type` accepts.
`set_data` and `event_of::set_data` shall return `result<void>`, and `event_of::with_data` shall return `result<event_of>`.
