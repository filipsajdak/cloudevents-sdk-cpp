---
uid: SWR-EXT-0012
title: data_as reads a matching json_document without parsing
type: software
status: approved
priority: high
rationale: >
  data_as parsed the payload text on every call.
  A json_document built by the requested codec is already that codec's DOM, obtained through json_document::get<Codec>(), so the typed value can be read from it directly.
  A document from another codec is converted through text as SWR-JSON-0042 states.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-DESC-0001]
satisfied_by: [code:include/cloudevents/format/typed_payload.hpp]
verified_by: [test:test/typed_payload_test.cpp::data-as-reads-a-same-codec-document]
owner: filip.sajdak
version: 2
---
When the event data holds a json_document built by `Codec`, `data_as<T, Codec>` shall read the payload from that document through `json_document::get<Codec>()`, without serialising or parsing it.
