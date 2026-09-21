---
uid: SWR-NATS-0001
title: NATS carries the JSON event format and nothing else
type: software
status: approved
priority: medium
rationale: >
  The binding used to say NATS "will only support structured data mode at this time", because the protocol had no message headers. NATS 2.2 introduced them and the specification now says "Every compliant implementation SHOULD support both structured and binary modes", with binary mode mapping every attribute to a `ce-` prefixed header. Version 1 of this requirement forbade what the specification now asks for, which is what a requirement written against a frozen tag does when the tag moves.

  The event format is still not a choice: implementations "MUST support the JSON event format" and the binding names no other.
verification_method: test
security_classification: operational
derived_from: [SYS-NATS-0001]
satisfied_by: [code:include/cloudevents/binding/nats.hpp]
verified_by: [test:test/nats_binding_test.cpp::nats-structured-only, test:test/nats_binding_test.cpp::nats-binary-mode]
owner: filip.sajdak
version: 2
---
The NATS binding shall support structured mode carrying the JSON event format
serialization in the payload and binary mode mapping each attribute to a header
named for it with a `ce-` prefix, and shall offer no alternative event format.
