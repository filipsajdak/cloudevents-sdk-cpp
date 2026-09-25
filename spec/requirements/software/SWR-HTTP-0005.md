---
uid: SWR-HTTP-0005
title: Batched conversion variants over event sequences
type: software
status: approved
priority: medium
rationale: >
  SPEC 5.4 requires batched variants of both conversions so the batched content mode of
  the CloudEvents HTTP protocol binding can be produced and consumed without the caller
  assembling the JSON array by hand.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::batched-variants, test:test/http_binding_test.cpp::structured-body-read-in-place]
owner: filip.sajdak
version: 1
---
The binding shall provide batched variants of the conversions that take a sequence of
events and return one `message`, and that take one `message` and return a `result`
holding a vector of events.
