---
uid: STK-TRUST-0001
title: Defined behaviour on malformed input from an untrusted peer
type: stakeholder
status: approved
priority: high
rationale: >
  Every decode path parses an envelope that arrived over a network from a peer the
  service does not control. A parsing defect in this layer is directly reachable by
  an attacker, so failure has to be a typed error rather than undefined behaviour.
verification_method: test
security_classification: security-relevant
derived_from: []
satisfied_by: []
verified_by: [test:fuzz/fuzz_json_decode.cpp::fuzz-json-decode]
owner: filip.sajdak
version: 1
---
If input received from an untrusted peer is malformed, then the SDK shall report a
typed error without crashing, reading outside an allocation, or leaking memory.
