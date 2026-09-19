---
uid: SWR-HTTP-0007
title: Binary mode carries context attributes as ce- prefixed headers
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 and the CloudEvents HTTP protocol binding section 3.1.3 map each context
  attribute onto a header whose name is the attribute name prefixed with `ce-`, which is
  what lets an intermediary route on an event without parsing the body.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::binary-mode-ce-headers]
owner: filip.sajdak
version: 1
---
When producing a `message` in binary mode, the binding shall emit each context
attribute and each extension attribute as a header named `ce-` followed by the
attribute name in lower case.
