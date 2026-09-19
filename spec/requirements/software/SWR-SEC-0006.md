---
uid: SWR-SEC-0006
title: Round-trip property holds for generated events in every mode
type: software
status: approved
priority: high
rationale: >
  SPEC 6 requires decode(encode(e)) to equal e for generated events across all modes.
  A property over generated inputs reaches attribute combinations no hand-written
  fixture enumerates, which is where encoder and decoder disagreements hide.
verification_method: test
security_classification: operational
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:test/roundtrip_test.cpp::roundtrip-property]
owner: filip.sajdak
version: 1
---
The SDK shall reproduce an event exactly when that event is encoded and then decoded
again, for generated events in the structured, binary and batched modes.
