---
uid: SYS-HTTP-0001
title: HTTP protocol binding in three content modes
type: system
status: approved
priority: high
rationale: >
  HTTP is the transport the CloudEvents ecosystem standardised first and the one
  the documented interoperability corpus covers. Mapping events to a
  transport-neutral message keeps the SDK free of any HTTP library dependency.
verification_method: test
security_classification: security-relevant
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::http-binding-spec-examples]
owner: filip.sajdak
version: 1
---
The SDK shall map events to and from a transport-neutral message in the binary,
structured and batched content modes defined by the HTTP protocol binding, without
performing any network input or output itself.
