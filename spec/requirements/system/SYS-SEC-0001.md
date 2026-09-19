---
uid: SYS-SEC-0001
title: Decode paths withstand hostile input
type: system
status: approved
priority: high
rationale: >
  The decode surface (timestamp parsing, base64, percent-decoding, UTF-8 validation
  and JSON traversal) is reachable by any peer that can send a request. These are the
  routines where a memory-safety defect would become a remotely triggerable
  vulnerability, so they carry a continuous fuzzing obligation.
verification_method: test
security_classification: security-relevant
derived_from: [STK-TRUST-0001]
satisfied_by: []
verified_by: [test:fuzz/fuzz_json_decode.cpp::fuzz-json-decode]
owner: filip.sajdak
version: 1
---
The SDK shall subject every routine that parses untrusted input to a fuzz target and
to address and undefined-behaviour sanitizers, and shall report malformed input as a
typed error.
