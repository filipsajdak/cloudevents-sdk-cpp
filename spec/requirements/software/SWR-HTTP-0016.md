---
uid: SWR-HTTP-0016
title: An opt-in literal value policy for the HTTP binding
type: software
status: approved
priority: high
rationale: >
  The HTTP binding specification section 3.1.3.2 requires space, double-quote, percent and
  anything outside U+0021-U+007E to be percent-encoded in a header value. Measured on
  2026-09-21 against sdk-go v2.15.2 and sdk-java main, neither SDK encodes on send or
  decodes on receive. A conformant sender is therefore misread by both: an event whose
  subject is "a b" arrives at a Go application as "a%20b". In the other direction a Go
  sender emitting a literal percent is rejected here as a truncated escape.
  The default stays conformant, because the specification is what a receiver is entitled
  to expect and changing it would alter behaviour a released version already has. The
  policy is a defaulted template parameter, so opting in is naming it and a caller who
  does not is unaffected.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-HTTP-0001]
satisfied_by: [code:include/cloudevents/binding/http.hpp]
verified_by: [test:test/http_binding_test.cpp::http-value-policy, test:test/interop_test.cpp::interop-http-binary-mode]
owner: filip.sajdak
version: 1
---
The HTTP binding shall accept a value policy selecting percent-encoded or literal header
values, shall use the percent-encoded policy when the caller names none, and shall refuse
a literal value containing a control character so that a header value cannot carry a line
break.
