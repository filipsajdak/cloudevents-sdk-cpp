---
uid: SYS-BIND-0001
title: One shared core for the rules every binding repeats
type: system
status: approved
priority: high
rationale: >
  CloudEvents describes HTTP, Kafka, AMQP, MQTT, NATS and WebSockets the same way: attributes become named fields under a prefix, the payload becomes the body, and structured mode puts the whole event in the body under a content type. Copying that per binding would put the attribute set, the emission order, the error precedence and the extension-name rule in as many places as there are transports, and a spec correction would then have to find all of them.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/binding/common.hpp]
verified_by: [test:test/binding_core_test.cpp::binding-core-round-trip]
owner: filip.sajdak
version: 1
---
The SDK shall express the binding rules that every protocol shares once, in a form a
binding parameterises with what its own transport defines, so that adding a binding
adds only the differences.
