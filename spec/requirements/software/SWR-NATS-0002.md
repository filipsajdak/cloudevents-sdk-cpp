---
uid: SWR-NATS-0002
title: The payload is the whole message
type: software
status: approved
priority: medium
rationale: >
  A NATS message has a subject and a payload and no header section, so a binding that returned the shared `message` type would hand the caller a header map that can never be transmitted. Someone would eventually set a field in it and wonder where the value went.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::nats-payload-is-the-whole-message]
owner: filip.sajdak
version: 1
---
The NATS binding shall exchange the payload as UTF-8 JSON text rather than as the
shared message type, and shall emit no header fields and no content type.
