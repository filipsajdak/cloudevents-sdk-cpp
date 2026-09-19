---
uid: SWR-HTTP-0014
title: Non-CloudEvents request reported as not_a_cloudevent
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 separates "this request was never an event" from "this event is broken" so a
  server can pass an unrelated request to its own routing instead of answering a
  protocol error, which a single malformed code would make impossible.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::not-a-cloudevent-distinct-from-malformed]
owner: filip.sajdak
version: 1
---
If a `message` carries no `ce-specversion` header and its `Content-Type` selects the
binary mode, the binding shall fail with `errc::not_a_cloudevent`, which is a distinct
code from the one reported for a malformed event.
