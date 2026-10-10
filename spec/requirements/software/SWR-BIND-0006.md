---
uid: SWR-BIND-0006
title: The message body is read against a media type the caller passes in
type: software
status: approved
priority: medium
rationale: >
  The body reader used to take the event under construction as an out-parameter
  and read its `datacontenttype` to decide whether the body was JSON, so every
  binding had to set that attribute before calling it, an ordering nothing but a
  comment enforced. CR-0001 makes the event immutable once built, so the reader
  takes the media type as an argument and returns the payload, and the caller
  builds the event only once both are known.
  In ce::v3 the reader stored a JSON body unparsed as json_text.
  In ce::v4 it also takes the codec and the decode options, because a body under a JSON media type is parsed and stored as a json_document within the retention limit, so a binary-mode message yields the same payload as a structured one (CR-0004, owner decision of 2026-09-28).
  SWR-BIND-0007 states the limit, SWR-BIND-0008 the refusal of a body that is not JSON and SWR-BIND-0010 the empty body.
  The reader returns a result because the parse can fail.
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::binding-core-round-trip, test:test/http_binding_test.cpp::http-binary-mode-json-body]
owner: filip.sajdak
version: 2
---
The binding core shall provide a body reader that takes the message body, the media type describing it, the decoding codec and the decode options as arguments and returns a result holding the payload.
When the media type is one that `is_json_content_type` accepts and the body is non-empty, the payload shall be a json_document built by the codec within the retention limit; otherwise the body is carried as bytes, and an empty body as no payload.
