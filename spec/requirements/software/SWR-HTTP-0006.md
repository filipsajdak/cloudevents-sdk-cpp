---
uid: SWR-HTTP-0006
title: Content mode detection from the Content-Type header
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 and the CloudEvents HTTP protocol binding sections 3.1 to 3.3 select the
  content mode from the media type alone; making the prefix test explicit keeps a
  parameterised media type such as `application/cloudevents+json; charset=utf-8` in the
  structured branch.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::content-mode-detection]
owner: filip.sajdak
version: 1
---
When decoding a `message`, the binding shall select the batched mode if the
`Content-Type` header value begins with `application/cloudevents-batch`, the structured
mode if it begins with `application/cloudevents`, and the binary mode in every other
case.
