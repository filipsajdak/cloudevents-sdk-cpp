---
uid: SWR-KAFKA-0010
title: The Kafka binding offers a moving receive entry point
type: software
status: implemented
delivered_in: v0.6.1
priority: medium
rationale: >
  A Kafka record's value can be a large non-JSON payload, and the consumer that forwards it benefits
  from not copying it on receive. SWR-BIND-0014 states the contract all three bindings share.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-from-message-moving-agrees-with-from-message]
owner: filip.sajdak
version: 1
---
The Kafka binding shall provide `kafka::from_message_moving<Codec>`, taking a `message` by rvalue reference and a `json::decode_options` defaulting to the retention limit and parsing of SWR-BIND-0009, and returning a `result<event>`.
