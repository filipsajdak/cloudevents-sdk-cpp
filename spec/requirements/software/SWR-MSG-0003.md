---
uid: SWR-MSG-0003
title: What a transport delivers and what the SDK produced are distinct types
type: software
status: approved
priority: high
rationale: >
  A transport hands the SDK arbitrary bytes, so the type holding them cannot carry
  an invariant. Fields the SDK emitted are a different case: a duplicated `ce-`
  field there is a defect in this library. ADR-0008 splits the two rather than
  making every insertion fallible, because a per-field result is something a
  server adapter has nothing useful to do with and would discard.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-MSG-0001]
satisfied_by: [code:include/cloudevents/message.hpp]
verified_by: [test:test/message_headers_test.cpp::headers-adopt-refuses-what-it-cannot-hold]
owner: filip.sajdak
version: 1
---
The message header shall provide a permissive `raw_headers` type for fields that
arrived from a transport and an invariant-holding `headers` type for fields the
SDK produced, with a fallible conversion from the first to the second.
