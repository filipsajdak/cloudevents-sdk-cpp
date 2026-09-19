---
uid: SWR-HTTP-0008
title: datacontenttype maps to Content-Type and never to a ce- header
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 and the CloudEvents HTTP protocol binding section 3.1.3 carve
  `datacontenttype` out of the `ce-` mapping and place it in `Content-Type`; emitting it
  in both places would let a receiver read two conflicting media types for one body.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::datacontenttype-header-mapping]
owner: filip.sajdak
version: 1
---
When producing a `message` in binary mode from an event carrying `datacontenttype`, the
binding shall emit that value as the `Content-Type` header while omitting any
`ce-datacontenttype` header.
