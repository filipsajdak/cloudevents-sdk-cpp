---
uid: SWR-HTTP-0010
title: Percent-encoding of binary-mode header values on send
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 and the CloudEvents HTTP protocol binding section 3.1.3.1 restrict a header
  value to printable ASCII, so an attribute holding a space, a quote or non-ASCII text
  has to be percent-encoded over its UTF-8 bytes to survive transport intact.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::header-value-percent-encoding]
owner: filip.sajdak
version: 1
---
When writing an attribute value into a `ce-` header, the binding shall percent-encode
the space character, the double quote character, the percent character and every octet
of the UTF-8 encoding of the value that lies outside printable ASCII.
