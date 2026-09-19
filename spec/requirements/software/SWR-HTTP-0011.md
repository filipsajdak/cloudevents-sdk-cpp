---
uid: SWR-HTTP-0011
title: Percent-decoding accepts any percent-encoded octet
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 and the CloudEvents HTTP protocol binding section 3.1.3.1 require a receiver
  to be liberal, because another SDK may encode octets this SDK would have left as
  literal text; rejecting those would break interoperability on the receive path.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::header-value-percent-decoding]
owner: filip.sajdak
version: 1
---
When reading a `ce-` header value, the binding shall decode every percent-encoded octet
it finds, including octets whose literal form the encoder would have left unencoded.
