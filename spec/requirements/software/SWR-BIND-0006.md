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
verification_method: test
security_classification: operational
derived_from: [SYS-BIND-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::binding-core-round-trip]
owner: filip.sajdak
version: 1
---
The binding core shall provide a body reader that takes the message body and the
media type describing it as arguments and returns the payload.
