---
uid: SWR-SEC-0001
title: Fuzz targets for every untrusted-input parser
type: software
status: approved
priority: high
rationale: >
  SPEC 6 names timestamp parsing, JSON decode, HTTP message decode and base64 decode
  as fuzz targets. These four routines are the whole surface an attacker can reach by
  sending a request, so each carries its own target seeded from the conformance
  fixtures.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-SEC-0001]
satisfied_by: []
verified_by: [test:fuzz/fuzz_json_decode.cpp::fuzz-json-decode]
owner: filip.sajdak
version: 1
---
The SDK shall provide a libFuzzer target for timestamp parsing, JSON decoding, HTTP
message decoding and base64 decoding, each seeded from the conformance fixtures and
each completing a ten-minute run without a crash, a leak or a timeout.
