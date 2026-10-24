---
uid: SWR-PROTO-0017
title: A fuzz target for the Protobuf decoder
type: software
status: reviewed
priority: high
rationale: >
  SWR-SEC-0001 requires a libFuzzer target for every decode path of untrusted input. The decoder
  parses bytes with lengths, varints, a map and an opaque Any, the places where a hand-written
  reader fails. The prototype's smoke run (1,000,000 mutated inputs under ASan and UBSan, no
  report) is not a campaign.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-PROTO-0001, SYS-SEC-0001]
satisfied_by: []
verified_by: []
owner: filip.sajdak
version: 1
---
The SDK shall provide a libFuzzer target for `protobuf_format::decode`, seeded from the recorded
goldens, replaying its seed corpus on every pull request and completing a ten-minute run without
a crash, a leak or a timeout in the scheduled daily job, and shall assert for every accepted
input that it re-encodes (SWR-PROTO-0012).
