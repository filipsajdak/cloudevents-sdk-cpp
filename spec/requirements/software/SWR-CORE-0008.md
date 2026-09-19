---
uid: SWR-CORE-0008
title: Timestamp parsing through a single CTRE pattern
type: software
status: approved
priority: high
rationale: >
  SPEC 5.1 mandates one CTRE pattern for `timestamp::parse` so the accepted RFC
  3339 grammar is stated in a single place, and the parser runs over untrusted
  event text arriving from the network.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/detail/timestamp.hpp]
verified_by: [test:test/core_test.cpp::core-timestamp-parse-ctre]
owner: filip.sajdak
version: 1
---
The `timestamp::parse` operation shall match its input against one CTRE pattern
describing the RFC 3339 date-time grammar and return a failed `result` for text
that pattern rejects.
