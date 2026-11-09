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
  CR-0010 added the exception: a datacontenttype that starts with application/cloudevents would make
  the Content-Type header declare structured mode, so SWR-BIND-0019 refuses the event instead.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::datacontenttype-header-mapping]
owner: filip.sajdak
version: 2
---
When producing a `message` in binary mode from an event carrying `datacontenttype` that
SWR-BIND-0019 does not refuse, the binding shall emit that value as the `Content-Type`
header while omitting any `ce-datacontenttype` header.
