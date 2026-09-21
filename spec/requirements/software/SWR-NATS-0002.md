---
uid: SWR-NATS-0002
title: The payload is the whole message
type: software
status: approved
priority: medium
rationale: >
  A pre-2.2 NATS message has a subject and a payload and no header section, so the structured entry points exchange text: returning the shared `message` type there would hand the caller a header map that cannot be transmitted, and someone would eventually set a field in it and wonder where the value went. Binary mode needs headers by definition, so its entry points use `message` and say in their name that they do.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::nats-payload-is-the-whole-message, test:test/nats_binding_test.cpp::nats-binary-mode]
owner: filip.sajdak
version: 2
---
The NATS binding shall exchange structured mode as UTF-8 JSON text through
entry points that emit no header fields, and shall exchange binary mode through
separate entry points taking and returning the shared message type.
