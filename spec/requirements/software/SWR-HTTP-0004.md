---
uid: SWR-HTTP-0004
title: Message to event conversion on receive
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 names `http::from_message(message, codec)` as the single receive-side entry
  point; it parses attacker-supplied headers and bodies, so it reports every failure as
  a typed error value instead of a thrown exception.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::from-message-roundtrip]
owner: filip.sajdak
version: 1
---
The binding shall provide `http::from_message(message, codec)` returning a `result`
holding the decoded event on success and a typed `error` on failure.
