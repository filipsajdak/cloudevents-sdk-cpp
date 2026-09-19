---
uid: STK-INTEROP-0001
title: Events interoperate with the other CloudEvents SDKs
type: stakeholder
status: approved
priority: high
rationale: >
  No official C++ CloudEvents SDK exists, so this implementation has no reference
  peer to agree with. Interoperability with the established Go, Java, Rust and C#
  SDKs is therefore the only external measure of correctness available, and it is
  what lets a C++ service join an existing event-driven estate.
verification_method: test
security_classification: operational
derived_from: []
satisfied_by: []
verified_by: [test:test/interop_test.cpp::interop-golden-corpus]
owner: filip.sajdak
version: 1
---
The SDK shall produce and consume CloudEvents v1.0.2 messages that the Go and Java
CloudEvents SDKs accept and produce without any intermediate transformation.
