---
uid: SWR-CORE-0032
title: json_document can be shared across threads without a data race
type: software
status: reviewed
priority: high
rationale: >
  Callers copy events into queues and hand them to other threads.
  Copies of a json_document share one DOM instead of duplicating it, so the DOM must never change once built.
  The reference count is then the only state that concurrent copies write.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The `json_document` type shall permit any number of threads to copy, compare and read the same document concurrently without a data race.
