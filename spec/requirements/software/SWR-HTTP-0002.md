---
uid: SWR-HTTP-0002
title: Headers as an ordered multimap with case-insensitive lookup
type: software
status: approved
priority: high
rationale: >
  SPEC 5.4 requires header order to survive a round trip while HTTP field names are
  case-insensitive, so the container has to preserve insertion order and duplicate
  names yet match a lookup key regardless of the casing on the wire.
verification_method: test
security_classification: operational
derived_from: [SYS-HTTP-0001]
satisfied_by: []
verified_by: [test:test/http_binding_test.cpp::headers-ordered-multimap]
owner: filip.sajdak
version: 1
---
The `headers` member of `message` shall be an ordered multimap that preserves
insertion order and repeated field names, and whose lookup operations match a field
name without regard to letter case.
