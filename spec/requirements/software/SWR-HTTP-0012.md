---
uid: SWR-HTTP-0012
title: Invalid UTF-8 in a decoded header value is an error
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 makes the decoded octet sequence a UTF-8 string; admitting an ill-formed
  sequence would put bytes into a `std::string` attribute that downstream JSON encoding
  and logging cannot represent, so the failure is reported at the boundary.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::header-value-invalid-utf8]
owner: filip.sajdak
version: 1
---
If the octet sequence produced by percent-decoding a `ce-` header value is not
well-formed UTF-8, the binding shall fail the conversion with a typed error rather than
returning an event.
