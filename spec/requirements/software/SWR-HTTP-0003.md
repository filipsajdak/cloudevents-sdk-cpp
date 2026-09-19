---
uid: SWR-HTTP-0003
title: Event to message conversion in a caller-selected content mode
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 names `http::to_message(event, mode, codec)` as the single send-side entry
  point, so the caller chooses binary or structured mode explicitly rather than the
  SDK guessing from the event contents.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::to-message-modes]
owner: filip.sajdak
version: 1
---
The binding shall provide `http::to_message(event, mode, codec)` returning a `message`
encoded in the content mode named by the `mode` argument using the supplied JSON codec.
