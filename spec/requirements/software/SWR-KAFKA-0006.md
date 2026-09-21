---
uid: SWR-KAFKA-0006
title: A record that is not a CloudEvent is distinct from a malformed one
type: software
status: approved
priority: high
rationale: >
  A topic carries whatever its producers put there, and a consumer subscribing to one is far more likely to meet a plain record than a corrupt CloudEvent. Reporting both the same way forces the consumer to treat an ordinary message as an error, so the two outcomes stay distinct here exactly as they are in the HTTP binding.
verification_method: test
security_classification: operational
derived_from: [SYS-KAFKA-0001]
satisfied_by: [code:include/cloudevents/binding/kafka.hpp]
verified_by: [test:test/kafka_binding_test.cpp::kafka-not-a-cloudevent]
owner: filip.sajdak
version: 1
---
When a received record carries no `ce_specversion` header and no CloudEvents
content type, the Kafka binding shall report `errc::not_a_cloudevent` rather than a
parse or validation failure.
