---
uid: SWR-NATS-0003
title: The subject is the application's
type: software
status: approved
priority: medium
rationale: >
  The NATS binding specification defines no mapping from an event to a subject; it shows an example subject and says nothing about how it was chosen. Inventing a derivation, from `type` or `source`, would produce subjects no other SDK publishes to, which is worse than having none.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::nats-payload-is-the-whole-message]
owner: filip.sajdak
version: 1
---
The NATS binding shall derive no subject from an event and shall take no subject
as a parameter.
