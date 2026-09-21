---
uid: SYS-NATS-0001
title: Events map to and from NATS message payloads
type: system
status: approved
priority: medium
rationale: >
  NATS is the fifth transport the CloudEvents project specifies and the cheapest one to support, because the protocol carries no headers and the binding is therefore the JSON event format and nothing else. An event published by another SDK onto a subject has to be readable here.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::nats-structured-only]
owner: filip.sajdak
version: 1
---
The SDK shall map an event to and from a NATS message payload, naming no NATS
client type, so that an event published by another SDK onto a subject is read back
unchanged.
