---
uid: SWR-CORE-0018
title: Only specversion 1.0 is accepted on receive, and no other can be produced
type: software
status: implemented
delivered_in: v0.4.0
priority: high
rationale: >
  SPEC 5.1 pins `specversion` to the value 1.0 and open decision D5 declines
  support for 0.3 on receive. Under ADR-0008 the produce side cannot express any
  other version at all: `spec_version` is a type with one inhabitant, so the
  remaining rule concerns text arriving from a peer.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/attributes.hpp]
verified_by: [test:test/core_test.cpp::core-specversion-is-1-0-only]
owner: filip.sajdak
version: 2
---
When a decoded `specversion` holds a value other than the string 1.0, the SDK
shall return a failed result reporting the unsupported version.
