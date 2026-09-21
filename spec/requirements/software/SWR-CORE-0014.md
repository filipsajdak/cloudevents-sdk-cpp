---
uid: SWR-CORE-0014
title: event is constructed through a factory or a builder, never as an aggregate
type: software
status: approved
priority: high
rationale: >
  CR-0001 found that a public aggregate lets a caller build an event CloudEvents
  v1.0.2 core section 3.1 forbids, and that the encode paths disagree about whether
  to re-check it. ADR-0008 moves validity into the attribute types, which leaves
  construction as the single place an event can come into existence.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-CORE-0001]
satisfied_by: [code:include/cloudevents/core.hpp]
verified_by: [test:test/core_test.cpp::core-event-required-attributes]
owner: filip.sajdak
version: 2
---
The `event` type shall come into existence only through `create`, taking
already-validated `id`, `source` and `type`, or through `builder`, exposing no
public data member.
