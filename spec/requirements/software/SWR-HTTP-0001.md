---
uid: SWR-HTTP-0001
title: Transport-neutral message type with headers and binary body
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 makes the binding produce a plain value rather than a framework request
  object, so the SDK carries no HTTP library dependency and any server or client
  library can be adapted by copying headers and body.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::message-type-shape]
owner: filip.sajdak
version: 1
---
The binding header shall define a `message` aggregate holding a `headers` member and
a `body` member of the core `binary` type, declared without naming any type from an
HTTP client or server library.
