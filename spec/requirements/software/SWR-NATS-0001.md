---
uid: SWR-NATS-0001
title: NATS carries the JSON event format and nothing else
type: software
status: approved
priority: medium
rationale: >
  The NATS binding specification states that NATS "will only support structured data mode at this time", because "the NATS protocol does not support custom message headers, necessary for binary mode", and that all implementations "MUST support the JSON event format". A content mode parameter would offer a caller a choice the transport does not have, and an event format parameter would offer one the specification does not allow.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::nats-structured-only]
owner: filip.sajdak
version: 1
---
The NATS binding shall put the JSON event format serialization in the message
payload, and shall offer no content mode and no alternative event format.
