---
uid: SWR-HTTP-0009
title: Binary mode body is the unmodified event data
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 and the CloudEvents HTTP protocol binding section 3.1.4 define the binary
  mode body as the event data itself, so a consumer that ignores CloudEvents reads the
  same bytes the producer set.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::binary-mode-body-is-raw-data]
owner: filip.sajdak
version: 1
---
When producing a `message` in binary mode, the binding shall set the `body` member to
the event data bytes without wrapping, re-encoding or framing them.
