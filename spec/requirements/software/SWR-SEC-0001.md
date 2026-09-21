---
uid: SWR-SEC-0001
title: Fuzz targets for every untrusted-input parser
type: software
status: approved
priority: high
rationale: >
  SPEC 6 names timestamp parsing, JSON decode, HTTP message decode and base64 decode
  as fuzz targets. Each new protocol binding adds a decode path to that surface, so
  Kafka record decoding and NATS payload decoding carry targets too. The Kafka target
  is not a duplicate of the HTTP one: the two bindings take opposite branches of every
  compile-time choice in the shared core, so byte-exact key matching and UTF-8
  validation on the way in are unreachable from the HTTP target.

  The run splits by cadence because the two runs answer different questions. Replaying
  the seed corpus answers "did a defect we already found come back", costs a second,
  and belongs on every pull request. A ten-minute run explores inputs nobody has seen,
  which is worth the wall clock only where nobody is waiting on it, so it runs nightly
  and on demand. A release is cut from a commit a nightly run has covered.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:fuzz/fuzz_json_decode.cpp::fuzz-json-decode]
owner: filip.sajdak
version: 3
---
The SDK shall provide a libFuzzer target for timestamp parsing, JSON decoding, base64
decoding, and the decode path of every protocol binding, each seeded from the
conformance fixtures, each replaying its seed corpus on every pull request, and each
completing a ten-minute run without a crash, a leak or a timeout in a scheduled daily
job.
