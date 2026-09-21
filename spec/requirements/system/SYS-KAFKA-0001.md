---
uid: SYS-KAFKA-0001
title: Events map to and from Kafka records
type: system
status: approved
priority: high
rationale: >
  Kafka is the transport the other major CloudEvents SDKs ship after HTTP, and an event produced by the Go or Java SDK onto a topic has to be readable here. The binding needs no Kafka client library: a record is header fields plus an opaque value, which is the message shape the SDK already has.
verification_method: test
security_classification: operational
derived_from: [STK-INTEROP-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-binary-mode]
owner: filip.sajdak
version: 1
---
The SDK shall map an event to and from a Kafka record in binary and structured
mode, naming no Kafka client type, so that an event written by another SDK onto a
topic is read back unchanged.
