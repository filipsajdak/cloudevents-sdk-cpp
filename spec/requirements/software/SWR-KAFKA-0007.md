---
uid: SWR-KAFKA-0007
title: A record carries an optional key beside the message
type: software
status: approved
priority: medium
rationale: >
  A Kafka record has a key, which decides its partition, and the message type deliberately has only header fields and a body. Adding a key member to `message` would change a type every binding shares and that SWR-HTTP-0001 pins by static assertion, so the key is carried by composition instead.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-record-and-key-mapper]
owner: filip.sajdak
version: 1
---
The Kafka binding shall provide a `record` type holding a `message` and an optional
key, and shall leave the shared `message` type unchanged.
