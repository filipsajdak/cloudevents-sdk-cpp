---
uid: SYS-MSG-0001
title: One transport-neutral message shape for every binding
type: system
status: approved
priority: high
rationale: >
  A Kafka record, an AMQP message, an MQTT PUBLISH and an HTTP request are all a name/value list plus an opaque payload, so one shape serves them all and a caller that keeps every binding behind one seam does not need a type per transport. The shapes differ in one respect the SDK must not paper over: HTTP field names are case-insensitive and the others are not.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/message.hpp]
verified_by: [test:test/http_binding_test.cpp::headers-case-sensitive-lookup]
owner: filip.sajdak
version: 1
---
The SDK shall provide one message type carrying an ordered list of named fields and
an opaque payload, usable by every protocol binding, and shall offer both
case-insensitive and byte-exact lookup so that a binding can use the rule its own
transport defines.
